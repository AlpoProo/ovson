#include "ChatHook.h"
#include "Commands.h"
#include "../Utils/Logger.h"
#include "../Java.h"

// who-related capture removed; no interceptor linkage needed here

static jmethodID g_sendChatMessage = nullptr;

static void JNICALL onMethodEntry(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method)
{
	if (method != g_sendChatMessage)
		return;
}

// removed onMethodEntryChat

bool ChatHook::install()
{
	if (!lc->jvmti || !lc->env)
		return false;
	Logger::info("Installing chat hook");

	jclass playerCls = lc->GetClass("net.minecraft.client.entity.EntityPlayerSP");
	if (!playerCls){
		Logger::error("EntityPlayerSP not found");
		return false;
	}
	g_sendChatMessage = lc->env->GetMethodID(playerCls, "sendChatMessage", "(Ljava/lang/String;)V");
	if (!g_sendChatMessage){
		Logger::error("sendChatMessage method not found");
		return false;
	}

	jvmtiCapabilities caps{};
	caps.can_generate_method_entry_events = 1;
	jvmtiError capRes = lc->jvmti->AddCapabilities(&caps);
	if (capRes != JVMTI_ERROR_NONE){
		Logger::error("JVMTI AddCapabilities failed: %d", (int)capRes);
	}
	jvmtiEventCallbacks cbs{};

	jvmtiError cbRes = lc->jvmti->SetEventCallbacks(&cbs, sizeof(cbs));
	if (cbRes != JVMTI_ERROR_NONE){
		Logger::error("JVMTI SetEventCallbacks failed: %d", (int)cbRes);
		return false;
	}

	return true;
}

void ChatHook::uninstall()
{
	if (!lc->jvmti)
		return;
	lc->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
	jvmtiEventCallbacks cbs{};
	lc->jvmti->SetEventCallbacks(&cbs, sizeof(cbs));
}

bool ChatHook::onClientSendMessage(const std::string &message)
{
	if (CommandRegistry::instance().tryDispatch(message))
	{
		// command handled; block original send
		return true;
	}
	return false;
}
