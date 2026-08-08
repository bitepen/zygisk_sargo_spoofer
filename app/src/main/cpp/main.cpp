#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include "zygisk.hpp"

#define LOG_TAG "SargoSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::Option;

// ==========================================================
// 黑科技：截胡底层 SystemProperties，坚定地告诉它我们是 Pixel 3a
// ==========================================================
static jstring my_native_get(JNIEnv *env, jclass clazz, jstring key, jstring def) {
    if (!key) return def;
    const char *k = env->GetStringUTFChars(key, nullptr);
    jstring ret = nullptr;
    
    // 全面伪装成 Pixel 3a (sargo)
    if (strcmp(k, "ro.product.model") == 0 || strcmp(k, "ro.product.vendor.model") == 0) {
        ret = env->NewStringUTF("Pixel 3a");
    } else if (strcmp(k, "ro.product.brand") == 0 || strcmp(k, "ro.product.vendor.brand") == 0) {
        ret = env->NewStringUTF("google");
    } else if (strcmp(k, "ro.product.manufacturer") == 0 || strcmp(k, "ro.product.vendor.manufacturer") == 0) {
        ret = env->NewStringUTF("Google");
    } else if (strcmp(k, "ro.product.device") == 0 || strcmp(k, "ro.product.vendor.device") == 0) {
        ret = env->NewStringUTF("sargo");
    } else if (strcmp(k, "ro.product.name") == 0 || strcmp(k, "ro.product.vendor.name") == 0) {
        ret = env->NewStringUTF("sargo");
    } else {
        // 无关属性老老实实去系统里查
        char value[PROP_VALUE_MAX];
        if (__system_property_get(k, value) > 0) {
            ret = env->NewStringUTF(value);
        }
    }
    
    env->ReleaseStringUTFChars(key, k);
    return ret ? ret : def;
}

static jstring my_native_get1(JNIEnv *env, jclass clazz, jstring key) {
    return my_native_get(env, clazz, key, nullptr);
}
// ==========================================================

class SargoSpoofer : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;

        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process) {
            if (strstr(process, "android.apps.photos") != nullptr) {
                is_target = true;
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }

        if (is_target) {
            int dirfd = api->getModuleDir();
            if (dirfd >= 0) {
                dex_fd = openat(dirfd, "classes.dex", O_RDONLY);
                if (dex_fd >= 0) {
                    struct stat sb;
                    if (fstat(dex_fd, &sb) == 0 && sb.st_size > 0) {
                        dex_size = sb.st_size;
                        api->exemptFd(dex_fd); 
                    } else {
                        close(dex_fd);
                        dex_fd = -1;
                    }
                }
            }
        } else {
            api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!is_target) return;
        
        LOGI("PostAppSpecialize: Photos target confirmed. Injecting Build Props...");
        injectBuildProps(); 

        LOGI("PostAppSpecialize: Hooking SystemProperties (The Ultimate Bypass)...");
        JNINativeMethod methods[] = {
            {"native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void *)my_native_get},
            {"native_get", "(Ljava/lang/String;)Ljava/lang/String;", (void *)my_native_get1}
        };
        api->hookJniNativeMethods(env, "android/os/SystemProperties", methods, 2);
        
        if (dex_fd >= 0 && dex_size > 0) {
            LOGI("PostAppSpecialize: Mapping Dex into memory...");
            void *dex_map = mmap(nullptr, dex_size, PROT_READ, MAP_PRIVATE, dex_fd, 0);
            if (dex_map != MAP_FAILED) {
                jobject byte_buffer = env->NewDirectByteBuffer(dex_map, dex_size);
                jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
                jmethodID getSystemClassLoader = env->GetStaticMethodID(classLoaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
                jobject systemLoader = env->CallStaticObjectMethod(classLoaderClass, getSystemClassLoader);
                jclass inMemoryDexClassLoaderClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
                jmethodID init = env->GetMethodID(inMemoryDexClassLoaderClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
                jobject dexLoader = env->NewObject(inMemoryDexClassLoaderClass, init, byte_buffer, systemLoader);

                jmethodID loadClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                jstring className = env->NewStringUTF("com.lighty.sargospoofer.SystemFeatureHook");
                jclass hookClass = (jclass) env->CallObjectMethod(dexLoader, loadClass, className);

                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                } else if (hookClass) {
                    jmethodID initHook = env->GetStaticMethodID(hookClass, "init", "()V");
                    if (initHook) {
                        env->CallStaticVoidMethod(hookClass, initHook);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        } else {
                            LOGI("PostAppSpecialize: Java Hook successfully executed.");
                        }
                    }
                }
                env->DeleteLocalRef(className);
            }
            close(dex_fd);
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool is_target = false;
    int dex_fd = -1;
    size_t dex_size = 0;

    void setStaticString(jclass clazz, const char *name, const char *value) {
        jfieldID fid = env->GetStaticFieldID(clazz, name, "Ljava/lang/String;");
        if (fid) {
            jstring jstr = env->NewStringUTF(value);
            env->SetStaticObjectField(clazz, fid, jstr);
            env->DeleteLocalRef(jstr);
        }
    }

    void injectBuildProps() {
        jclass build_class = env->FindClass("android/os/Build");
        if (!build_class) return;
        setStaticString(build_class, "BRAND", "google");
        setStaticString(build_class, "MANUFACTURER", "Google");
        setStaticString(build_class, "DEVICE", "sargo");
        setStaticString(build_class, "PRODUCT", "sargo");
        setStaticString(build_class, "MODEL", "Pixel 3a");
        setStaticString(build_class, "FINGERPRINT", "google/sargo/sargo:12/SP2A.220505.002/8353555:user/release-keys");
        env->DeleteLocalRef(build_class);
    }
};

REGISTER_ZYGISK_MODULE(SargoSpoofer)