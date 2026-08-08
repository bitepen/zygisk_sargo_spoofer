package com.lighty.sargospoofer;

import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.List;

public class SystemFeatureHook {
    private static final String TAG = "SargoSpoofer-Java";

    public static void init() {
        Log.i(TAG, "========== Java Hook Init Started ==========");
        try {
            // 1. Android 15/16 “元反射”解禁隐藏 API
            Log.i(TAG, "Step 1: Bypassing Hidden API...");
            Method forName = Class.class.getDeclaredMethod("forName", String.class);
            Method getDeclaredMethod = Class.class.getDeclaredMethod("getDeclaredMethod", String.class, Class[].class);

            Class<?> vmRuntimeClass = (Class<?>) forName.invoke(null, "dalvik.system.VMRuntime");
            Method getRuntime = (Method) getDeclaredMethod.invoke(vmRuntimeClass, "getRuntime", null);
            Object vmRuntime = getRuntime.invoke(null);

            Method setHiddenApiExemptions = (Method) getDeclaredMethod.invoke(vmRuntimeClass, "setHiddenApiExemptions", new Class[]{String[].class});
            setHiddenApiExemptions.invoke(vmRuntime, new Object[]{new String[]{"L"}});
            Log.i(TAG, "Step 1: Hidden API bypassed successfully!");

            // 2. 拿到系统的 PackageManager
            Log.i(TAG, "Step 2: Fetching sPackageManager...");
            Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
            Field sPackageManagerField = activityThreadClass.getDeclaredField("sPackageManager");
            sPackageManagerField.setAccessible(true);
            final Object originalPackageManager = sPackageManagerField.get(null);

            if (originalPackageManager == null) {
                Log.e(TAG, "FATAL ERROR: originalPackageManager is NULL! Hook aborted.");
                return;
            }
            Log.i(TAG, "Step 2: sPackageManager found: " + originalPackageManager.getClass().getName());

            // 3. 动态代理
            Log.i(TAG, "Step 3: Creating Proxy...");
            Class<?> iPackageManagerInterface = Class.forName("android.content.pm.IPackageManager");
            Object proxy = Proxy.newProxyInstance(
                    iPackageManagerInterface.getClassLoader(),
                    new Class<?>[]{iPackageManagerInterface},
                    new InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
                            if ("hasSystemFeature".equals(method.getName()) && args != null && args.length >= 1) {
                                String featureName = (String) args[0];
                                
                                List<String> featuresToEnable = Arrays.asList(
                                        "com.google.android.apps.photos.NEXUS_PRELOAD",
                                        "com.google.android.apps.photos.nexus_preload",
                                        "com.google.android.feature.PIXEL_EXPERIENCE",
                                        "com.google.android.apps.photos.PIXEL_PRELOAD",
                                        "com.google.android.apps.photos.PIXEL_2016_PRELOAD",
                                        "com.google.android.feature.PIXEL_2017_EXPERIENCE",
                                        "com.google.android.apps.photos.PIXEL_2017_PRELOAD",
                                        "com.google.android.feature.PIXEL_2018_EXPERIENCE",
                                        "com.google.android.apps.photos.PIXEL_2018_PRELOAD",
                                        "com.google.android.feature.PIXEL_2019_MIDYEAR_EXPERIENCE",
                                        "com.google.android.feature.PIXEL_2019_EXPERIENCE",
                                        "com.google.android.apps.photos.PIXEL_2019_PRELOAD"
                                );
                                if (featuresToEnable.contains(featureName)) {
                                    Log.d(TAG, "Intercepted TRUE for feature: " + featureName);
                                    return true;
                                }
                                
                                if (featureName != null && featureName.startsWith("com.google.android.feature.PIXEL_202")) {
                                    Log.d(TAG, "Intercepted FALSE for feature: " + featureName);
                                    return false;
                                }
                            }
                            return method.invoke(originalPackageManager, args);
                        }
                    }
            );

            // 4. 替换回去
            sPackageManagerField.set(null, proxy);
            Log.i(TAG, "Step 4: Proxy successfully injected into ActivityThread!");
            Log.i(TAG, "========== Java Hook Init Finished ==========");

        } catch (Throwable e) {
            Log.e(TAG, "FATAL ERROR during Java Hook injection!", e);
        }
    }
}