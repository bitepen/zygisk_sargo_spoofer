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
            // 1. 解除 Android 隐藏 API 限制
            try {
                Method getRuntime = Class.forName("dalvik.system.VMRuntime").getDeclaredMethod("getRuntime");
                Object vmRuntime = getRuntime.invoke(null);
                Method setHiddenApiExemptions = vmRuntime.getClass().getDeclaredMethod("setHiddenApiExemptions", String[].class);
                setHiddenApiExemptions.invoke(vmRuntime, new Object[]{new String[]{"L"}});
            } catch (Throwable t) {
                // Ignore
            }

            // 2. 拿到系统的 PackageManager 核心
            Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
            Field sPackageManagerField = activityThreadClass.getDeclaredField("sPackageManager");
            sPackageManagerField.setAccessible(true);
            final Object originalPackageManager = sPackageManagerField.get(null);

            if (originalPackageManager == null) return;

            // 3. 制作一个假的 PackageManager (动态代理)
            Class<?> iPackageManagerInterface = Class.forName("android.content.pm.IPackageManager");
            Object proxy = Proxy.newProxyInstance(
                    iPackageManagerInterface.getClassLoader(),
                    new Class<?>[]{iPackageManagerInterface},
                    new InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
                            // 只要相册敢问 hasSystemFeature，我们就截胡
                            if ("hasSystemFeature".equals(method.getName()) && args != null && args.length > 0) {
                                String featureName = (String) args[0];
                                
                                // 强制放行 Pixel 3a 时代的福利标志
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
                                    return true;
                                }
                                
                                // 强制屏蔽 2020 年以后的新特性，防穿帮
                                if (featureName != null && featureName.startsWith("com.google.android.feature.PIXEL_202")) {
                                    return false;
                                }
                            }
                            // 其他无关紧要的盘问，交给原版系统处理
                            return method.invoke(originalPackageManager, args);
                        }
                    }
            );

            // 4. 把假的代理掉包换进系统
            sPackageManagerField.set(null, proxy);
        } catch (Throwable e) {
            // 静默失败，绝对不影响相册运行
        }
    }
}