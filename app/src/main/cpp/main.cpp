#include <jni.h>
#include <string_view>
#include <android/log.h>
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

        if (!is_target) {
            // 如果不是相册，立马卸载，不占用一点内存！
            api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!is_target) return;
        
        LOGD("Google Photos detected! Applying Pixel 3a (sargo) properties...");
        injectBuildProps();
        
        // 接下来我们要预留一个位置
        // 用于动态加载一段小巧的 Dex 来拦截 PackageManager.hasSystemFeature
        // LOGD("Preparing to inject inline Java hook...");
    }

private:
    Api *api;
    JNIEnv *env;
    bool is_target = false;

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

        // 精准覆盖为 Pixel 3a
        setStaticString(build_class, "BRAND", "google");
        setStaticString(build_class, "MANUFACTURER", "Google");
        setStaticString(build_class, "DEVICE", "sargo");
        setStaticString(build_class, "PRODUCT", "sargo");
        setStaticString(build_class, "MODEL", "Pixel 3a");
        setStaticString(build_class, "FINGERPRINT", "google/sargo/sargo:12/SP2A.220505.002/8353555:user/release-keys");

        env->DeleteLocalRef(build_class);
        LOGD("Build properties modified successfully.");
    }
};

REGISTER_ZYGISK_MODULE(SargoSpoofer)