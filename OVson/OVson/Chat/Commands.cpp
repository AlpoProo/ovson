#include "Commands.h"
#include "ChatSDK.h"
#include "../Utils/Logger.h"
#include "../Config/Config.h"
#include "ChatInterceptor.h"
#include "../Services/Hypixel.h"
#include "../Render/ClickGUI.h"
#include "../Logic/BedDefense/BedDefenseManager.h"
#include <sstream>
#include <iomanip>

CommandRegistry &CommandRegistry::instance()
{
	static CommandRegistry inst;
	return inst;
}

void CommandRegistry::registerCommand(const std::string &name, CommandHandler handler)
{
	nameToHandler[name] = std::move(handler);
}

bool CommandRegistry::tryDispatch(const std::string &message)
{
	if (message.empty() || message[0] != '.')
		return false;
	std::string rest = message.substr(1);
	std::istringstream iss(rest);
	std::string cmd;
	iss >> cmd;
	std::string args;
	std::getline(iss, args);
	if (!args.empty() && args[0] == ' ')
		args.erase(0, 1);

	auto it = nameToHandler.find(cmd);
	if (it == nameToHandler.end()){
		ChatSDK::showPrefixed(std::string("§cUnknown command: §f.") + cmd);
		return true;
	}
	try{
		it->second(args);
	}
	catch (...){
		Logger::error("Command threw: %s", cmd.c_str());
	}
	return true;
}

void CommandRegistry::forEachCommand(const std::function<void(const std::string &)> &visitor)
{
	for (std::unordered_map<std::string, CommandHandler>::const_iterator it = nameToHandler.begin(); it != nameToHandler.end(); ++it)
	{
		visitor(it->first);
	}
}

namespace
{
	void cmd_echo(const std::string &args){
		ChatSDK::showPrefixed(args);
	}

	void cmd_help(const std::string &args){
		(void)args;
		std::string list;
		CommandRegistry::instance().forEachCommand([&](const std::string &name)
													   {
			if (!list.empty()) list += ", ";
			list += "." + name; });
		if (list.empty())
			list = "(no commands)";
		ChatSDK::showPrefixed(std::string("§7Commands: §f") + list);
	}

	void cmd_api(const std::string &args){
		std::string trimmed = args;
		while (!trimmed.empty() && trimmed.front() == ' ')
			trimmed.erase(trimmed.begin());
		while (!trimmed.empty() && trimmed.back() == ' ')
			trimmed.pop_back();
		if (trimmed.rfind("new ", 0) == 0)
		{
			std::string key = trimmed.substr(4);
			Config::setApiKey(key);
			ChatSDK::showPrefixed("API key updated.");
			return;
		}
		const std::string &key = Config::getApiKey();
		if (key.empty())
			ChatSDK::showPrefixed("No API key set. Use .api new <key>");
		else
			ChatSDK::showPrefixed(std::string("API KEY: §f") + key);
	}

	void cmd_mode(const std::string &args){
		std::string a = args;
		while (!a.empty() && a.front() == ' ')
			a.erase(a.begin());
		if (a == "bedwars")
		{
			ChatInterceptor::setMode(0);
			ChatSDK::showPrefixed("mode: bedwars");
			return;
		}
		if (a == "skywars")
		{
			ChatInterceptor::setMode(1);
			ChatSDK::showPrefixed("mode: skywars");
			return;
		}
		if (a == "duels")
		{
			ChatInterceptor::setMode(2);
			ChatSDK::showPrefixed("mode: duels");
			return;
		}
		ChatSDK::showPrefixed("usage: .mode bedwars|skywars|duels");
	}
	void cmd_ovmode(const std::string &args){
		std::string a = args;
		while (!a.empty() && a.front() == ' ')
			a.erase(a.begin());
		while (!a.empty() && a.back() == ' ')
			a.pop_back();
		if (a == "gui")
		{
			Config::setOverlayMode("gui");
			ChatSDK::showPrefixed("§aOverlay mode: §fGUI");
			return;
		}
		if (a == "chat")
		{
			Config::setOverlayMode("chat");
			ChatSDK::showPrefixed("§aOverlay mode: §fChat");
			return;
		}
		if (a == "invisible")
		{
			Config::setOverlayMode("invisible");
			ChatSDK::showPrefixed("§aOverlay mode: §fInvisible");
			return;
		}
		ChatSDK::showPrefixed("§cusage: §f.ovmode gui|chat|invisible");
	}

	void cmd_tab(const std::string &args){
		std::string a = args;
		while (!a.empty() && a.front() == ' ') a.erase(a.begin());
		while (!a.empty() && a.back() == ' ') a.pop_back();

		if (a == "on") {
			Config::setTabEnabled(true);
			ChatSDK::showPrefixed("§aTab Stats: §fEnabled");
			return;
		}
		if (a == "off") {
			Config::setTabEnabled(false);
			ChatSDK::showPrefixed("§cTab Stats: §fDisabled");
			return;
		}
		if (a == "fk" || a == "fkdr" || a == "wins" || a == "wlr") {
			Config::setTabMode(a);
			ChatSDK::showPrefixed("§aTab Mode set to: §f" + a);
			return;
		}
		ChatSDK::showPrefixed("§cusage: §f.tab on|off|fk|fkdr|wins|wlr");
	}

	void cmd_debugging(const std::string &args){
		std::string a = args;
		while (!a.empty() && a.front() == ' ') a.erase(a.begin());
		while (!a.empty() && a.back() == ' ') a.pop_back();

		if (a == "on") {
			Config::setGlobalDebugEnabled(true);
			ChatSDK::showPrefixed("§aDebugging: §fEnabled");
			return;
		}
		if (a == "off") {
			Config::setGlobalDebugEnabled(false);
			ChatSDK::showPrefixed("§cDebugging: §fDisabled");
			return;
		}
		ChatSDK::showPrefixed("§cusage: §f.debugging on|off");
	}

	void cmd_stats(const std::string &args){
		std::string playerName = args;
		while (!playerName.empty() && playerName.front() == ' ')
			playerName.erase(playerName.begin());
		while (!playerName.empty() && playerName.back() == ' ')
			playerName.pop_back();

		if (playerName.empty()) {
			ChatSDK::showPrefixed("§cusage: §f.stats <player>");
			return;
		}

		ChatSDK::showPrefixed("§7Fetching stats for §f" + playerName + "§7...");
		
		auto uuidOpt = Hypixel::getUuidByName(playerName);
		if (!uuidOpt.has_value()) {
			ChatSDK::showPrefixed("§cPlayer not found: §f" + playerName);
			return;
		}

		std::string apiKey = Config::getApiKey();
		if (apiKey.empty()) {
			ChatSDK::showPrefixed("§cNo API key set. Use §f.api new <key>");
			return;
		}

		auto statsOpt = Hypixel::getPlayerStats(apiKey, uuidOpt.value());
		if (!statsOpt.has_value()) {
			ChatSDK::showPrefixed("§cFailed to fetch stats for §f" + playerName);
			return;
		}

		Hypixel::PlayerStats stats = statsOpt.value();
		// calc (short for calculator)
		double fkdr = (stats.bedwarsFinalDeaths == 0) ? (double)stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
		double wlr = (stats.bedwarsLosses == 0) ? (double)stats.bedwarsWins : (double)stats.bedwarsWins / stats.bedwarsLosses;

		std::ostringstream fkdrSs, wlrSs;
		fkdrSs << std::fixed << std::setprecision(2) << fkdr;
		wlrSs << std::fixed << std::setprecision(2) << wlr;

		std::string msg = ChatSDK::formatPrefix();
		msg += "§f" + playerName + " §7- ";
		msg += "§6" + std::to_string(stats.bedwarsStar) + "✫ ";
		msg += "§7[§fFKDR§7] §c" + fkdrSs.str() + " ";
		msg += "§7[§fFK§7] §c" + std::to_string(stats.bedwarsFinalKills) + " ";
		msg += "§7[§fWins§7] §a" + std::to_string(stats.bedwarsWins) + " ";
		msg += "§7[§fWLR§7] §a" + wlrSs.str();

		ChatSDK::showClientMessage(msg);
	}

	void cmd_clickgui(const std::string& args) {
		std::string a = args;
		while (!a.empty() && a.front() == ' ') a.erase(a.begin());
		if (a == "on") {
			Config::setClickGuiOn(true);
			std::string key = Render::ClickGUI::getKeyName(Config::getClickGuiKey());
			ChatSDK::showPrefixed("§aClickGUI: §fEnabled (" + key + " will open menu)");
			return;
		}
		if (a == "off") {
			Config::setClickGuiOn(false);
			std::string key = Render::ClickGUI::getKeyName(Config::getClickGuiKey());
			ChatSDK::showPrefixed("§cClickGUI: §fDisabled (" + key + " will open overlay)");
			return;
		}
		ChatSDK::showPrefixed("usage: .clickgui on|off");
	}
}

void cmd_bedplates(const std::string& args) {
	std::string trimmed = args;
	while (!trimmed.empty() && trimmed.front() == ' ')
		trimmed.erase(trimmed.begin());
	while (!trimmed.empty() && trimmed.back() == ' ')
		trimmed.pop_back();
	
	BedDefense::BedDefenseManager* manager = BedDefense::BedDefenseManager::getInstance();
	
	if (trimmed == "on") {
		Config::setBedDefenseEnabled(true);
		manager->enable();
		ChatSDK::showPrefixed("§aBed Defense nameplates enabled");
	}
	else if (trimmed == "off") {
		Config::setBedDefenseEnabled(false);
		manager->disable();
		ChatSDK::showPrefixed("§cBed Defense nameplates disabled");
	}
	else {
		bool current = Config::isBedDefenseEnabled();
		ChatSDK::showPrefixed(std::string("§7Bed Defense: ") + (current ? "§aON" : "§cOFF"));
		ChatSDK::showPrefixed("§7Usage: §f.bedplates on§7 or §f.bedplates off");
	}
}

void cmd_bedscan(const std::string& args) {
	(void)args;
	ChatSDK::showPrefixed("§7Manually triggering bed scan...");
	BedDefense::BedDefenseManager* manager = BedDefense::BedDefenseManager::getInstance();
	manager->onWorldChange();
	manager->forceScan();
}

void cmd_lookat(const std::string& args) {
	(void)args;
	if (!lc) return;
	JNIEnv* env = lc->getEnv();
	if (!env) return;

	try {
		jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
		jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
		if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
		jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
		
		jfieldID f_mouse = env->GetFieldID(mcCls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
		if (!f_mouse) f_mouse = env->GetFieldID(mcCls, "field_71476_x", "Lnet/minecraft/util/MovingObjectPosition;");
		
		jobject mop = f_mouse ? env->GetObjectField(mcObj, f_mouse) : nullptr;
		if (mop) {
			jclass mopCls = lc->GetClass("net.minecraft.util.MovingObjectPosition");
			jfieldID f_type = env->GetFieldID(mopCls, "typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
			if (!f_type) f_type = env->GetFieldID(mopCls, "field_72313_a", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
			
			jobject hitType = f_type ? env->GetObjectField(mop, f_type) : nullptr;
			if (hitType) {
				jclass typeCls = lc->GetClass("net.minecraft.util.MovingObjectPosition$MovingObjectType");
				jfieldID f_block = env->GetStaticFieldID(typeCls, "BLOCK", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
				jobject blockType = env->GetStaticObjectField(typeCls, f_block);
				
				if (env->IsSameObject(hitType, blockType)) {
					jfieldID f_bpos = env->GetFieldID(mopCls, "blockPos", "Lnet/minecraft/util/BlockPos;");
					if (!f_bpos) f_bpos = env->GetFieldID(mopCls, "field_178783_e", "Lnet/minecraft/util/BlockPos;");
					
					jobject bpos = f_bpos ? env->GetObjectField(mop, f_bpos) : nullptr;
					if (bpos) {
						jclass bposCls = lc->GetClass("net.minecraft.util.BlockPos");
						jmethodID m_getX = env->GetMethodID(bposCls, "getX", "()I");
						jmethodID m_getY = env->GetMethodID(bposCls, "getY", "()I");
						jmethodID m_getZ = env->GetMethodID(bposCls, "getZ", "()I");
						
						int bx = env->CallIntMethod(bpos, m_getX);
						int by = env->CallIntMethod(bpos, m_getY);
						int bz = env->CallIntMethod(bpos, m_getZ);
						
						std::string name = BedDefense::BedDefenseManager::getInstance()->getBlockName(bx, by, bz);
						int meta = BedDefense::BedDefenseManager::getInstance()->getBlockMetadata(bx, by, bz);
						
						std::string debugInfo = "§7ID: §f?";
						try {
							jclass worldCls = lc->GetClass("net.minecraft.world.World");
							jmethodID m_getState = env->GetMethodID(worldCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
							if (!m_getState) m_getState = env->GetMethodID(worldCls, "func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
							
							jobject mcCls = lc->GetClass("net.minecraft.client.Minecraft");
							jfieldID theMc = env->GetStaticFieldID((jclass)mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
							if (!theMc) theMc = env->GetStaticFieldID((jclass)mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
							jobject mcObj = env->GetStaticObjectField((jclass)mcCls, theMc);
							
							jfieldID f_world = env->GetFieldID((jclass)mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
							if (!f_world) f_world = env->GetFieldID((jclass)mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
							jobject world = env->GetObjectField(mcObj, f_world);
							
							jobject state = env->CallObjectMethod(world, m_getState, bpos);
							if (state) {
								jclass stateCls = lc->GetClass("net.minecraft.block.state.IBlockState");
								jmethodID m_getBlock = env->GetMethodID(stateCls, "getBlock", "()Lnet/minecraft/block/Block;");
								if (!m_getBlock) m_getBlock = env->GetMethodID(stateCls, "func_177230_c", "()Lnet/minecraft/block/Block;");
								jobject block = env->CallObjectMethod(state, m_getBlock);
								
								if (block) {
									jclass blockCls = lc->GetClass("net.minecraft.block.Block");
									jmethodID m_getId = env->GetStaticMethodID(blockCls, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
									if (!m_getId) m_getId = env->GetStaticMethodID(blockCls, "func_149682_b", "(Lnet/minecraft/block/Block;)I");
									int id = env->CallStaticIntMethod(blockCls, m_getId, block);
									
									jmethodID m_getUnlocalized = env->GetMethodID(blockCls, "getUnlocalizedName", "()Ljava/lang/String;");
                                    if (!m_getUnlocalized) m_getUnlocalized = env->GetMethodID(blockCls, "func_149739_a", "()Ljava/lang/String;");
                                    jstring jUnloc = (jstring)env->CallObjectMethod(block, m_getUnlocalized);
                                    const char* unlocStr = env->GetStringUTFChars(jUnloc, 0);
                                    std::string uName = unlocStr ? unlocStr : "unknown";
                                    env->ReleaseStringUTFChars(jUnloc, unlocStr);

									jclass objCls = env->GetObjectClass(block);
									jmethodID m_getClass = env->GetMethodID(env->FindClass("java/lang/Object"), "getClass", "()Ljava/lang/Class;");
									jclass clsObj = (jclass)env->CallObjectMethod(block, m_getClass);
									jmethodID m_getName = env->GetMethodID(env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
									jstring clsName = (jstring)env->CallObjectMethod(clsObj, m_getName);
									
									const char* clsUtf = env->GetStringUTFChars(clsName, 0);
									std::string cName = clsUtf ? clsUtf : "unknown";
									env->ReleaseStringUTFChars(clsName, clsUtf);
									
									debugInfo = "§7ID: §f" + std::to_string(id) + " §7Name: §f" + uName + " §7Class: §f" + cName;
									env->DeleteLocalRef(block);
									env->DeleteLocalRef(objCls);
									env->DeleteLocalRef(clsObj);
									env->DeleteLocalRef(clsName);
                                    if (jUnloc) env->DeleteLocalRef(jUnloc);
								}
								env->DeleteLocalRef(state);
							}
							env->DeleteLocalRef(world);
							env->DeleteLocalRef(mcObj);
						} catch(...) {}

						ChatSDK::showPrefixed("§7LookAt: §f" + name + " §7(Meta: §f" + std::to_string(meta) + "§7)");
						ChatSDK::showPrefixed(debugInfo + " §7at §f" + std::to_string(bx) + "," + std::to_string(by) + "," + std::to_string(bz));
						env->DeleteLocalRef(bpos);
					}
				} else {
					ChatSDK::showPrefixed("§7Not looking at a block.");
				}
				env->DeleteLocalRef(hitType);
				env->DeleteLocalRef(blockType);
			}
			env->DeleteLocalRef(mop);
		}
		env->DeleteLocalRef(mcObj);
	} catch (...) {
		ChatSDK::showPrefixed("§cException in lookat command.");
	}
}

void RegisterDefaultCommands(){
	CommandRegistry::instance().registerCommand("echo", cmd_echo);
	CommandRegistry::instance().registerCommand("help", cmd_help);
	CommandRegistry::instance().registerCommand("api", cmd_api);
	CommandRegistry::instance().registerCommand("mode", cmd_mode);
	CommandRegistry::instance().registerCommand("ovmode", cmd_ovmode);
	CommandRegistry::instance().registerCommand("tab", cmd_tab);
	CommandRegistry::instance().registerCommand("debugging", cmd_debugging);
	CommandRegistry::instance().registerCommand("stats", cmd_stats);
	CommandRegistry::instance().registerCommand("clickgui", cmd_clickgui);
	CommandRegistry::instance().registerCommand("bedplates", cmd_bedplates);
	CommandRegistry::instance().registerCommand("bedscan", cmd_bedscan);
	CommandRegistry::instance().registerCommand("lookat", cmd_lookat);
}
