#pragma once
#include "../Java.h"

namespace FocusFix {
    static void setIngameFocus(bool focus) {
        JNIEnv* env = lc->getEnv();
        if (!env) return;

        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        if (!mcCls) return;

        jfieldID f_mc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!f_mc) f_mc = env->GetStaticFieldID(mcCls, "field_71412_C", "Lnet/minecraft/client/Minecraft;");
        jobject mc = env->GetStaticObjectField(mcCls, f_mc);
        if (!mc) return;

        jfieldID f_focus = env->GetFieldID(mcCls, "inGameHasFocus", "Z");
        if (!f_focus) f_focus = env->GetFieldID(mcCls, "field_71415_G", "Z");
        
        if (f_focus) {
            env->SetBooleanField(mc, f_focus, focus);
        }

        if (!focus) {
            jfieldID f_leftClick = env->GetFieldID(mcCls, "leftClickCounter", "I");
            if (!f_leftClick) f_leftClick = env->GetFieldID(mcCls, "field_71429_W", "I");
            if (f_leftClick) env->SetIntField(mc, f_leftClick, 10000);

            jfieldID f_rightClick = env->GetFieldID(mcCls, "rightClickDelayTimer", "I");
            if (!f_rightClick) f_rightClick = env->GetFieldID(mcCls, "field_71467_ac", "I");
            if (f_rightClick) env->SetIntField(mc, f_rightClick, 10000);
        } else {
            jfieldID f_leftClick = env->GetFieldID(mcCls, "leftClickCounter", "I");
            if (!f_leftClick) f_leftClick = env->GetFieldID(mcCls, "field_71429_W", "I");
            if (f_leftClick) env->SetIntField(mc, f_leftClick, 0);

            jfieldID f_rightClick = env->GetFieldID(mcCls, "rightClickDelayTimer", "I");
            if (!f_rightClick) f_rightClick = env->GetFieldID(mcCls, "field_71467_ac", "I");
            if (f_rightClick) env->SetIntField(mc, f_rightClick, 0);
        }

        env->DeleteLocalRef(mc);
    }
}
