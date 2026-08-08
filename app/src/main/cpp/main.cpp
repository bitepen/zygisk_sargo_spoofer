#include <jni.h>
#include <string_view>
#include <android/log.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "zygisk.hpp"

#define LOG_TAG "SargoSpoofer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

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
            if (std::string_view(process) == "com.google.android.apps.photos") {
                is_target = true;
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }

        if (is_target) {
            // Zygote 阶段：打开咱们的 classes.dex 并保护文件句柄
            int dirfd = api->getModuleDir();
            if (dirfd >= 0) {
                dex_fd = openat(dirfd, "classes.dex", O_RDONLY);
                if (dex_fd >= 0) {
                    struct stat sb;
                    if (fstat(dex_fd, &sb) == 0) {
                        dex_size = sb.st_size;
                        api->exemptFd(dex_fd); // 告诉系统别关这个文件
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
        
        LOGD("Google Photos detected! Applying Pixel 3a (sargo) properties...");
        injectBuildProps(); // 第一道门：改名牌
        
        // 第二道门：内存加载 classes.dex 执行拦截
        if (dex_fd >= 0 && dex_size > 0) {
            void *dex_map = mmap(nullptr, dex_size, PROT_READ, MAP_PRIVATE, dex_fd, 0);
            if (dex_map != MAP_FAILED) {
                jobject byte_buffer = env->NewDirectByteBuffer(dex_map, dex_size);

                // 获取系统类加载器
                jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
                jmethodID getSystemClassLoader = env->GetStaticMethodID(classLoaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
                jobject systemLoader = env->CallStaticObjectMethod(classLoaderClass, getSystemClassLoader);

                // 实例化 InMemoryDexClassLoader
                jclass inMemoryDexClassLoaderClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
                jmethodID init = env->GetMethodID(inMemoryDexClassLoaderClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
                jobject dexLoader = env->NewObject(inMemoryDexClassLoaderClass, init, byte_buffer, systemLoader);

                // 加载我们的拦截类
                jmethodID loadClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                jstring className = env->NewStringUTF("com.lighty.sargospoofer.SystemFeatureHook");
                jclass hookClass = (jclass) env->CallObjectMethod(dexLoader, loadClass, className);

                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    LOGD("Failed to load hook class!");
                } else if (hookClass) {
                    // 调用静态 init 方法
                    jmethodID initHook = env->GetStaticMethodID(hookClass, "init", "()V");
                    if (initHook) {
                        env->CallStaticVoidMethod(hookClass, initHook);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        } else {
                            LOGD("Java Hook successfully injected and executed!");
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