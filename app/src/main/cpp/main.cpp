#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "zygisk.hpp"

#define LOG_TAG "SargoSpoofer"
// 强行提升日志级别到 INFO 和 ERROR，击穿安卓 16 的屏蔽！
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::Option;

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
            // 使用更底层的 strcmp 比较包名，防漏判
            if (strcmp(process, "com.google.android.apps.photos") == 0) {
                is_target = true;
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }

        if (is_target) {
            LOGI("PreAppSpecialize: Photos detected! Trying to open classes.dex...");
            int dirfd = api->getModuleDir();
            if (dirfd >= 0) {
                dex_fd = openat(dirfd, "classes.dex", O_RDONLY);
                if (dex_fd >= 0) {
                    struct stat sb;
                    if (fstat(dex_fd, &sb) == 0 && sb.st_size > 0) {
                        dex_size = sb.st_size;
                        api->exemptFd(dex_fd); 
                        LOGI("PreAppSpecialize: classes.dex opened! Size: %zu bytes", dex_size);
                    } else {
                        close(dex_fd);
                        dex_fd = -1;
                        LOGE("PreAppSpecialize: classes.dex is empty or corrupted!");
                    }
                } else {
                    LOGE("PreAppSpecialize: Could NOT find classes.dex! Check the ZIP file structure.");
                }
            } else {
                LOGE("PreAppSpecialize: Failed to get module directory.");
            }
        } else {
            api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!is_target) return;
        
        LOGI("PostAppSpecialize: Process starting! Applying Build props (Door 1)...");
        injectBuildProps(); 
        
        if (dex_fd >= 0 && dex_size > 0) {
            LOGI("PostAppSpecialize: Mapping Dex into memory (Door 2)...");
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
                    LOGE("PostAppSpecialize: FAILED to load Java Hook class!");
                } else if (hookClass) {
                    jmethodID initHook = env->GetStaticMethodID(hookClass, "init", "()V");
                    if (initHook) {
                        env->CallStaticVoidMethod(hookClass, initHook);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            LOGE("PostAppSpecialize: Java Hook init() threw an exception!");
                        } else {
                            LOGI("PostAppSpecialize: SUCCESS! Java Hook injected and running!");
                        }
                    } else {
                        LOGE("PostAppSpecialize: init() method missing in Java code.");
                    }
                }
                env->DeleteLocalRef(className);
            } else {
                LOGE("PostAppSpecialize: mmap failed!");
            }
            close(dex_fd);
        } else {
            LOGE("PostAppSpecialize: Dex FD invalid, skipping Door 2 (Hook failed).");
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
        LOGI("PostAppSpecialize: Build properties modified successfully.");
    }
};

REGISTER_ZYGISK_MODULE(SargoSpoofer)