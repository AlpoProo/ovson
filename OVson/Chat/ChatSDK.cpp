#include "ChatSDK.h"
#include "../Utils/Logger.h"
#include "../Java.h"
#include <cstdarg>
#include <vector>

static bool callSendChatMessage(const std::string &text)
{
	CMinecraft mc;
	CPlayer player = mc.GetLocalPlayer();
	if (!lc->env || !lc->GetClass("net.minecraft.client.entity.EntityPlayerSP"))
		return false;

	jclass playerCls = lc->GetClass("net.minecraft.client.entity.EntityPlayerSP");
	jmethodID sendChat = lc->env->GetMethodID(playerCls, "sendChatMessage", "(Ljava/lang/String;)V");
	if (!sendChat)
		return false;
	jstring jtext = lc->env->NewStringUTF(text.c_str());
	lc->env->CallVoidMethod(player.Get(), sendChat, jtext);
	lc->env->DeleteLocalRef(jtext);
	player.Cleanup();
	return true;
}

static bool callAddChatMessage(const std::string &text)
{
	if (!lc->env)
		return false;
	jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
	if (!mcCls)
		return false;
	jfieldID theMc = lc->env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
	jobject mcObj = lc->env->GetStaticObjectField(mcCls, theMc);
	jfieldID f_ingame = lc->env->GetFieldID(mcCls, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
	jobject ingame = lc->env->GetObjectField(mcObj, f_ingame);
	// get chat gui
	jclass igCls = lc->GetClass("net.minecraft.client.gui.GuiIngame");
	jmethodID getChatGUI = lc->env->GetMethodID(igCls, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;");
	jobject chatGui = lc->env->CallObjectMethod(ingame, getChatGUI);
	jclass cctCls = lc->GetClass("net.minecraft.util.ChatComponentText");
	jmethodID cctCtor = lc->env->GetMethodID(cctCls, "<init>", "(Ljava/lang/String;)V");
	jstring jtext = lc->env->NewStringUTF(text.c_str());
	jobject component = lc->env->NewObject(cctCls, cctCtor, jtext);
	jclass gncCls = lc->GetClass("net.minecraft.client.gui.GuiNewChat");
	jmethodID print = lc->env->GetMethodID(gncCls, "printChatMessage", "(Lnet/minecraft/util/IChatComponent;)V");

	lc->env->CallVoidMethod(chatGui, print, component);
	lc->env->DeleteLocalRef(jtext);
	lc->env->DeleteLocalRef(component);
	lc->env->DeleteLocalRef(chatGui);
	lc->env->DeleteLocalRef(ingame);
	lc->env->DeleteLocalRef(mcObj);
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
