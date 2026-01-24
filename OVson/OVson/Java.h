#pragma once
#include <jni.h>
#include <jvmti.h>
#include <mutex>
#include <unordered_map>
#include <string>
#include <iostream>
#include <algorithm>
#include <memory>

class Lunar {
public:
    JavaVM* vm;
    jvmtiEnv* jvmti;

    Lunar() : vm(nullptr), jvmti(nullptr) {}

    JNIEnv* getEnv() {
        if (!vm) return nullptr;
        JNIEnv* env = nullptr;
        jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
                return nullptr;
            }
        }
        return env;
    }

    void GetLoadedClasses() {
        JNIEnv* env = getEnv();
        if (!vm || !env) return;
        if (vm->GetEnv((void**)&jvmti, JVMTI_VERSION_1_2) != JNI_OK) return;

        jclass lang = env->FindClass("java/lang/Class");
        if (!lang) return;
        jmethodID getName = env->GetMethodID(lang, "getName", "()Ljava/lang/String;");

        jclass* classesPtr = nullptr;
        jint amount = 0;
        if (jvmti->GetLoadedClasses(&amount, &classesPtr) != JVMTI_ERROR_NONE) {
            env->DeleteLocalRef(lang);
            return;
        }

        Cleanup();

        for (int i = 0; i < amount; i++) {
            jstring name = (jstring)env->CallObjectMethod(classesPtr[i], getName);
            if (name) {
                const char* classNameUtf = env->GetStringUTFChars(name, 0);
                if (classNameUtf) {
                    std::string className(classNameUtf);
                    std::replace(className.begin(), className.end(), '/', '.');
                    
                    jclass globalCls = (jclass)env->NewGlobalRef(classesPtr[i]);
                    classes[className] = globalCls;
                    
                    env->ReleaseStringUTFChars(name, classNameUtf);
                }
                env->DeleteLocalRef(name);
            }
        }
        
        if (classesPtr) jvmti->Deallocate((unsigned char*)classesPtr);
        env->DeleteLocalRef(lang);

        GetClass("java.util.Collection");
        GetClass("java.util.Iterator");
        GetClass("net.minecraft.client.Minecraft");
        GetClass("net.minecraft.client.network.NetworkPlayerInfo");
        GetClass("net.minecraft.util.ChatComponentText");
        GetClass("net.minecraft.client.gui.GuiIngame");
        GetClass("net.minecraft.client.gui.GuiPlayerTabOverlay");
        GetClass("net.minecraft.util.IChatComponent");
        GetClass("net.minecraft.client.gui.GuiChat");
        GetClass("net.minecraft.client.gui.GuiScreen");
        GetClass("net.minecraft.client.gui.GuiTextField");
    }

    jclass GetClass(const std::string& className) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        JNIEnv* env = getEnv();
        if (!env) return nullptr;
        auto it = classes.find(className);
        if (it != classes.end())
            return it->second;
        
        std::string internalName = className;
        std::replace(internalName.begin(), internalName.end(), '.', '/');
        jclass localCls = env->FindClass(internalName.c_str());
        if (localCls) {
            jclass globalCls = (jclass)env->NewGlobalRef(localCls);
            classes[className] = globalCls;
            env->DeleteLocalRef(localCls);
            if (env->ExceptionCheck()) env->ExceptionClear();
            return globalCls;
        }
        
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    bool CheckException() {
        JNIEnv* env = getEnv();
        if (env && env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        }
        return false;
    }

    void Cleanup() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        JNIEnv* env = getEnv();
        if (!env) return;
        for (auto& pair : classes) {
            env->DeleteGlobalRef(pair.second);
        }
        classes.clear();
    }

private:
    std::unordered_map<std::string, jclass> classes;
    std::recursive_mutex m_mutex;
};

inline auto lc = std::make_unique<Lunar>();