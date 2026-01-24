#include "ChatSDK.h"
#include "../Utils/Logger.h"
#include "../Java.h"
#include <cstdarg>
#include <vector>

static bool callSendChatMessage(const std::string &text)
{
	JNIEnv* env = lc->getEnv();
	if (!env) return false;
	CMinecraft mc;
	CPlayer player = mc.GetLocalPlayer();
	if (!player.Get()) return false;

	jclass playerCls = lc->GetClass("net.minecraft.client.entity.EntityPlayerSP");
    if (!playerCls) {
        player.Cleanup();
        return false;
    }

	jmethodID sendChat = env->GetMethodID(playerCls, "sendChatMessage", "(Ljava/lang/String;)V");
    if (!sendChat) sendChat = env->GetMethodID(playerCls, "func_71165_d", "(Ljava/lang/String;)V");
    
	if (!sendChat) {
		player.Cleanup();
		return false;
	}

	jstring jtext = env->NewStringUTF(text.c_str());
	env->CallVoidMethod(player.Get(), sendChat, jtext);
	env->DeleteLocalRef(jtext);
	player.Cleanup();
	return true;
}

static bool callAddChatMessage(const std::string &text)
{
	JNIEnv* env = lc->getEnv();
	if (!env)
		return false;
	jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
	if (!mcCls)
		return false;
	
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
    if (!theMc) return false;

	jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj) return false;

	jfieldID f_ingame = env->GetFieldID(mcCls, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!f_ingame) f_ingame = env->GetFieldID(mcCls, "field_71456_v", "Lnet/minecraft/client/gui/GuiIngame;");
	
    if (!f_ingame) { env->DeleteLocalRef(mcObj); return false; }

    jobject ingame = env->GetObjectField(mcObj, f_ingame);
    if (!ingame) { env->DeleteLocalRef(mcObj); return false; }

	// get chat gui
	jclass igCls = lc->GetClass("net.minecraft.client.gui.GuiIngame");
    if (!igCls) { env->DeleteLocalRef(ingame); env->DeleteLocalRef(mcObj); return false; }

	jmethodID getChatGUI = env->GetMethodID(igCls, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;");
    if (!getChatGUI) getChatGUI = env->GetMethodID(igCls, "func_146158_b", "()Lnet/minecraft/client/gui/GuiNewChat;");

	if (!getChatGUI) { env->DeleteLocalRef(ingame); env->DeleteLocalRef(mcObj); return false; }

    jobject chatGui = env->CallObjectMethod(ingame, getChatGUI);
    if (!chatGui) { env->DeleteLocalRef(ingame); env->DeleteLocalRef(mcObj); return false; }

	jclass cctCls = lc->GetClass("net.minecraft.util.ChatComponentText");
	jmethodID cctCtor = env->GetMethodID(cctCls, "<init>", "(Ljava/lang/String;)V");
	jstring jtext = env->NewStringUTF(text.c_str());
	jobject component = env->NewObject(cctCls, cctCtor, jtext);

	jclass gncCls = lc->GetClass("net.minecraft.client.gui.GuiNewChat");
	jmethodID print = env->GetMethodID(gncCls, "printChatMessage", "(Lnet/minecraft/util/IChatComponent;)V");
    if (!print) print = env->GetMethodID(gncCls, "func_146227_a", "(Lnet/minecraft/util/IChatComponent;)V");

    if (print) {
	    env->CallVoidMethod(chatGui, print, component);
    }

	env->DeleteLocalRef(jtext);
	env->DeleteLocalRef(component);
	env->DeleteLocalRef(chatGui);
	env->DeleteLocalRef(ingame);
	env->DeleteLocalRef(mcObj);
	return true;
}

bool ChatSDK::sendClientChat(const std::string &text)
{
	return callSendChatMessage(text);
}

bool ChatSDK::showClientMessage(const std::string &text)
{
	return callAddChatMessage(text);
}

std::string ChatSDK::formatPrefix()
{
	const char *S = "\xC2\xA7";
	return std::string(S) + "0[" + S + "r" + S + "cO" + S + "6V" + S + "es" + S + "ao" + S + "bn" + S + "0]" + S + "r ";
}

bool ChatSDK::showPrefixed(const std::string &message)
{
	return showClientMessage(formatPrefix() + "§f" + message);
}

bool ChatSDK::showPrefixedf(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	return showPrefixed(buf);
}
