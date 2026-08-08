package com.lighty.sargospoofer;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;
import java.util.List;

public class SystemFeatureHook {
    public static void init() {
        try {
            // 1. Android 15/16 必须使用的“元反射”解禁隐藏 API
            Method forName = Class.class.getDeclaredMethod("forName", String.class);
            Method getDeclaredMethod = Class.class.getDeclaredMethod("getDeclaredMethod", String.class, Class[].class);

            Class<?> vmRuntimeClass = (Class<?>) forName.invoke(null, "dalvik.system.VMRuntime");
            Method getRuntime = (Method) getDeclaredMethod.invoke(vmRuntimeClass, "getRuntime", null);
            Object vmRuntime = getRuntime.invoke(null);

            Method setHiddenApiExemptions = (Method) getDeclaredMethod.invoke(vmRuntimeClass, "setHiddenApiExemptions", new Class[]{String[].class});
            setHiddenApiExemptions.invoke(vmRuntime, new Object[]{new String[]{"L"}});

            // 2. 拦截核心：掉包 sPackageManager
            Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
            Field sPackageManagerField = activityThreadClass.getDeclaredField("sPackageManager");
            sPackageManagerField.setAccessible(true);
            final Object originalPackageManager = sPackageManagerField.get(null);

            if (originalPackageManager == null) return;

            Class<?> iPackageManagerInterface = Class.forName("android.content.pm.IPackageManager");
            Object proxy = Proxy.newProxyInstance(
                    iPackageManagerInterface.getClassLoader(),
                    new Class<?>[]{iPackageManagerInterface},
                    new InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
                            // IPackageManager.hasSystemFeature 实际上是 hasSystemFeature(String name, int version)
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
                                    return true; // 强行放行
                                }
                                
                                if (featureName != null && featureName.startsWith("com.google.android.feature.PIXEL_202")) {
                                    return false; // 强行屏蔽
                                }
                            }
                            return method.invoke(originalPackageManager, args);
                        }
                    }
            );

            // 3. 把代理换回系统
            sPackageManagerField.set(null, proxy);
        } catch (Throwable e) {
            // 失败时静默
        }
    }
}