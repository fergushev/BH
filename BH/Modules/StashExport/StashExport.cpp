﻿#include "StashExport.h"
#include "../../D2Ptrs.h"
#include "../../BH.h"
#include "../../D2Stubs.h"
#include "../Item/ItemDisplay.h"
#include "../Item/Item.h"
#include "../../TableReader.h"
#include "../../MPQInit.h"
#include "../../Constants.h"
#include <algorithm>

map<std::string, Toggle> StashExport::Toggles;
map<std::string, std::unique_ptr<Mustache::AMustacheTemplate>> StashExport::MustacheTemplates;
UnitAny* StashExport::viewingUnit;

#define NAMEOF(statid) (AllStatList[statid]->name)

using namespace Drawing;

void StashExport::OnLoad() {
	LoadConfig();

	settingsTab = new UITab("StashExport", BH::settingsUI);

	unsigned int x = 8;
	unsigned int y = 7;
	int keyhook_x = 310;

	new Checkhook(settingsTab, x, y, &Toggles["Include Equipment"].state, "Include Equipment");
	new Checkhook(settingsTab, x, (y += 15), &Toggles["Include Fixed Stats"].state, "Include Fixed Stats");
	new Checkhook(settingsTab, x, (y += 15), &Toggles["Condense Stats"].state, "Condense Stats");
	new Checkhook(settingsTab, x, (y += 15), &Toggles["Export On Menu"].state, "Export On Menu");

	// the MustacheTemplates will not be reloaded
	options.clear();
	options.push_back("json");

	BH::config->ReadAssoc("Mustache", mustaches);
	BH::config->ReadString("Mustache Default", dfltExprt);
	int idx = 0;

	for (auto it = mustaches.cbegin(); it != mustaches.cend(); it++) {
		auto t = Mustache::parse(it->second);
		if (t) {
			idx++;
			if (dfltExprt.compare(it->first) == 0) {
				exportType = idx;
			}
			MustacheTemplates[it->first] = std::unique_ptr<Mustache::AMustacheTemplate>(t);
			options.push_back(it->first);
		}
	}

	new Combohook(settingsTab, x, (y += 15), 150, &exportType, options);
}

void StashExport::LoadConfig() {
	BH::config->ReadToggle("Include Equipment", "None", true, Toggles["Include Equipment"]);
	BH::config->ReadToggle("Include Fixed Stats", "None", false, Toggles["Include Fixed Stats"]);
	BH::config->ReadToggle("Condense Stats", "None", true, Toggles["Condense Stats"]);
	BH::config->ReadToggle("Export On Menu", "None", false, Toggles["Export On Menu"]);
	BH::config->ReadToggle("Export Gear", "None", false, Toggles["Export Gear"]);
}

void StashExport::OnUnload() {

}

void StashExport::OnLoop() {
}

bool checkFlag(UnitAny* pItem, DWORD flag) {
	return (pItem->pItemData->dwFlags & flag) > 0;
}

JSONObject* StashExport::getStatEntry(WORD statId, WORD statId2, DWORD statVal, DWORD min, DWORD max) {
	if (!statVal)
		return nullptr;
	int hi = statVal >> 8;
	int lo = statVal & 0xff;
	auto entry = new JSONObject();
	if (min != max) {
		auto range = new JSONObject();
		range->set("min", (int)min);
		range->set("max", (int)max);
		entry->set("range", range);
	}
	//else if (min && max && !Toggles["Include Fixed Stats"].state) {
	//	return nullptr;
	//}

	switch (statId) {
	case STAT_SINGLESKILL:
	case STAT_NONCLASSSKILL: {
		auto sk = Tables::Skills.binarySearch("Id", statId2);
		if (sk) {
			entry->set("name", NAMEOF(statId));
			entry->set("skill", sk->getString("skill"));
			entry->set("value", (int)statVal);
		}
	}
						   break;
	case STAT_SKILLTAB:
		for (int j = 0; j < 21; j++) {
			if (SKILL_TABS[j].id == statId2) {
				entry->set("name", NAMEOF(statId));
				entry->set("skill", SKILL_TABS[j].name);
				entry->set("value", (int)statVal);
				break;
			}
		}
		break;
	case STAT_CLASSSKILLS:
		for (int j = 0; j < 7; j++) {
			if (CLASS_NAMES[j].id == statId2) {
				entry->set("name", NAMEOF(statId));
				entry->set("skill", CLASS_NAMES[j].name);
				entry->set("value", (int)statVal);
			}
		}
		break;
	case STAT_CHARGED: {
		int level = statId2 & 0x3F;
		statId2 >>= 6;	// skill Id needs to be bit-shifted
		auto sk = Tables::Skills.binarySearch("Id", statId2);
		if (sk) {
			entry->set("name", NAMEOF(statId));
			entry->set("skill", sk->getString("skill"));
			entry->set("curCharges", lo);
			entry->set("maxCharges", hi);
			entry->set("level", level);
		}
	}
					 break;
	case STAT_SKILLONDEATH:
	case STAT_SKILLONHIT:
	case STAT_SKILLONKILL:
	case STAT_SKILLONLEVELUP:
	case STAT_SKILLONSTRIKING:
	case STAT_SKILLWHENSTRUCK: {
		int level = statId2 & 0x3F;
		statId2 >>= 6;	// skill Id needs to be bit-shifted
		auto sk = Tables::Skills.binarySearch("Id", statId2);
		if (sk) {
			entry->set("name", NAMEOF(statId));
			entry->set("skill", sk->getString("skill"));
			entry->set("chance%", lo);
			entry->set("level", level);
		}
	}
							 break;
	default:
		switch (statId) {
		case STAT_HP:
		case STAT_MAXHP:
		case STAT_MANA:
		case STAT_MAXMANA:
		case STAT_STAMINA:
		case STAT_MAXSTAMINA:
		case STAT_LIFEPERLEVEL:
		case STAT_MANAPERLEVEL:
			statVal = hi;
			break;
		default:
			break;
		}
		entry->set("name", NAMEOF(statId));
		entry->set("value", (int)statVal);
		break;
	}

	return entry;
}

void StashExport::fillStats(JSONArray* statsArray, JSONObject* itemDef, UnitAny* pItem, std::string codeKey, std::string paramKey, std::string minKey, std::string maxKey, int maxProps) {
	for (int rs = 1; rs < maxProps; rs++) {
		std::string code = string_format(codeKey, rs);
		std::string par = string_format(paramKey, rs);
		std::string min = string_format(minKey, rs);
		std::string max = string_format(maxKey, rs);
		auto skProps = Tables::Properties.findEntry("code", itemDef->getString(code));
		if (!skProps) { break; }
		for (int sk = 1; sk < 8; sk++) {
			char buf[6];
			sprintf_s(buf, "stat%d", sk);
			std::string funcKey = string_format("func%d", sk);
			auto skDef = Tables::ItemStatCost.findEntry("Stat", skProps->getString(buf));
			if (!skDef) { break; }
			int func = (int)skProps->getNumber(funcKey);

			auto statFunc = STAT_FUNCTIONS[1];
			if (func < MAX_FUNCTION) {
				statFunc = STAT_FUNCTIONS[func];
			}

			auto entry = statFunc(pItem, skProps, skDef, itemDef->get(par), (int)itemDef->getNumber(min), (int)itemDef->getNumber(max));
			statsArray->add(entry);
			if (entry && Toggles["Condense Stats"].state) {
				auto desc = skProps->getString("*desc");
				if (desc.length() > 0) {
					entry->set("name", desc);
				}
				else {
					entry->set("name", itemDef->getString(code));
				}
				break;
			}
		}
	}
}

void StashExport::GetItemInfo(UnitAny* pItem, JSONObject* pBuffer) {
	ItemsTxt* txt = D2COMMON_GetItemText(pItem->dwTxtFileNo);
	std::string type = UnicodeToAnsi(D2LANG_GetLocaleText(txt->wnamestr));
	// Remove hardcoded color
	if (type.size() > 3 && type.substr(0, 2) == "ÿc") {
		type = type.substr(3, type.size() - 3);
	}
	pBuffer->set("type", type);
	pBuffer->set("quality", std::string(QUALITY_NAMES[pItem->pItemData->dwQuality]));
	pBuffer->set("iLevel", (int)pItem->pItemData->dwItemLevel);

	for (int j = 0; j < 6; j++)
	{
		const auto location = ITEM_LOCATIONS[j];
		if (location.id != pItem->pItemData->ItemLocation) {
			continue;
		}

		if (location.id == STORAGE_NULL) {
			if (pItem->pItemData->NodePage == NODEPAGE_EQUIP) {
				pBuffer->set("location", std::string("Equip"));
			}

			break;
		}
		else {
			pBuffer->set("location", location.name);
		}

		break;
	}

	char cCode[] = {
		txt->szCode[0],
		txt->szCode[1],
		txt->szCode[2],
		txt->szCode[3],
		0
	};
	std::string code = cCode;
	code = Trim(code);

	int nCorruptor = ItemGetCorruptor(pItem);

	BYTE nType = txt->nType;
	if (nType >= 96 && nType <= 102) { // is gem?
		pBuffer->set("isGem", true);
	}
	else if (nType == 74) {	// is rune?
		pBuffer->set("isRune", true);
	}
	else {
		pBuffer->set("isEthereal", checkFlag(pItem, ITEM_ETHEREAL));
		pBuffer->set("unidentified", !checkFlag(pItem, ITEM_IDENTIFIED));
		pBuffer->set("sockets", (int)D2COMMON_GetUnitStat(pItem, STAT_SOCKETS, 0));
		pBuffer->set("defense", (int)D2COMMON_GetUnitStat(pItem, STAT_DEFENSE, 0));
		pBuffer->set("quantity", (int)D2COMMON_GetUnitStat(pItem, STAT_AMMOQUANTITY, 0));

		auto statsObject = new JSONArray();
		pBuffer->set("stats", statsObject);
		switch (pItem->pItemData->dwQuality) {
		case ITEM_QUALITY_MAGIC: {
			auto name = type;
			for (int i = 0; i < 3; i++) {
				const char* prefix = D2COMMON_GetItemMagicalMods(pItem->pItemData->wPrefix[i])->szName;
				const char* suffix = D2COMMON_GetItemMagicalMods(pItem->pItemData->wSuffix[i])->szName;
				if (prefix) {
					name = std::string(prefix) + " " + name;
				}
				if (suffix) {
					name = name + " " + suffix;
				}
			}

			// Remove hardcoded color
			if (name.size() > 3 && name.substr(0, 2) == "ÿc") {
				name = name.substr(3, name.size() - 3);
			}
			pBuffer->set("name", name);
		}
							   break;
		case ITEM_QUALITY_UNIQUE: {
			JSONObject* unDef = Tables::UniqueItems.entryAt(pItem->pItemData->dwFileIndex);
			if (unDef) {
				std::string name = unDef->getString("index");
				// Remove hardcoded color
				if (name.size() > 3 && name.substr(0, 2) == "ÿc") {
					name = name.substr(3, name.size() - 3);
				}
				pBuffer->set("name", name);
				fillStats(statsObject, unDef, pItem, "prop%d", "par%d", "min%d", "max%d", 13);
			}
		}
								break;
		case ITEM_QUALITY_SET: {
			JSONObject* setDef = Tables::SetItems.entryAt(pItem->pItemData->dwFileIndex);
			if (setDef) {
				std::string name = setDef->getString("index");
				// Remove hardcoded color
				if (name.size() > 3 && name.substr(0, 2) == "ÿc") {
					name = name.substr(3, name.size() - 3);
				}
				pBuffer->set("set", setDef->getString("set"));
				pBuffer->set("name", name);
				fillStats(statsObject, setDef, pItem, "prop%d", "par%d", "min%d", "max%d", 13);
			}
		}
							 break;
		case ITEM_QUALITY_CRAFT:
		case ITEM_QUALITY_RARE: {
			// -155 because that is how big the suffix table is? ... also -1 from that
			JSONObject* rarePrefix = Tables::RarePrefix.entryAt(pItem->pItemData->wRarePrefix - 156);
			// zero based vs 1 based?; or the table just doesn't have the header row
			JSONObject* rareSuffix = Tables::RareSuffix.entryAt(pItem->pItemData->wRareSuffix - 1);

			if (rarePrefix && rareSuffix) {
				pBuffer->set("name", rarePrefix->getString("name") + " " + rareSuffix->getString("name"));
			}
		}
							  break;
		default:
			break;
		}

		if (checkFlag(pItem, ITEM_RUNEWORD)) {
			pBuffer->set("isRuneword", true);
			std::string rwName = UnicodeToAnsi(D2LANG_GetLocaleText(pItem->pItemData->wPrefix[0]));
			pBuffer->set("runeword", rwName);

			RunesTxt* pRune = D2COMMON_ITEMS_GetRunesTxtRecordFromItem(pItem);
			JSONObject* rwDef = Tables::Runewords.findEntry("Name", pRune->szName);
			if (rwDef) {
				fillStats(statsObject, rwDef, pItem, "T1Code%d", "T1Param%d", "T1Min%d", "T1Max%d", 8);
			}
		}
		//else { //if (pItem->pItemData->dwQuality != ITEM_QUALITY_UNIQUE &&
			//pItem->pItemData->dwQuality != ITEM_QUALITY_SET) {
		DWORD value = 0;
		Stat* aStatList = new Stat[STAT_MAX];
		StatList* pStatList = D2COMMON_GetStatList(pItem, NULL, STAT_MAX);
		if (pStatList) {
			DWORD dwStats = D2COMMON_CopyStatList(pStatList, aStatList, STAT_MAX);
			for (UINT i = 0; i < dwStats; i++) {
				auto entry = getStatEntry(
					aStatList[i].wStatIndex,
					aStatList[i].wSubIndex,
					aStatList[i].dwStatValue,
					0, 0);
				if (entry->get("name")->toString() == "corruptor") {
					continue;
				}
				BOOL hasStat = FALSE;
				BOOL isCorrupted = StatIsCorrupted(aStatList[i].wStatIndex, nCorruptor);
				for (int i = 0; i < statsObject->length(); i++) {
					JSONObject* ent = statsObject->getObject(i);
					if (ent->equals(entry)) {
						ent->set("corrupted", isCorrupted);
						hasStat = TRUE;
						break;
					}
					else if (ent->get("name")->equals(entry->get("name")))
					{
						if (ent->find("skill")->toString() == "null" || ent->get("skill")->equals(entry->get("skill")))
						{
							int val1 = ent->get("value")->toInt();
							int val2 = entry->get("value")->toInt();
							if (val1 >= val2)
							{
								hasStat = TRUE;
								break;
							}
							else
							{
								hasStat = TRUE;
								ent->set("value", val2);
								ent->set("corrupted", isCorrupted);
								break;
							}
						}
					}
					else if (ent->get("name")->toString() == "dmg%" && (entry->get("name")->toString() == "item_maxdamage_percent" || entry->get("name")->toString() == "item_mindamage_percent"))
					{
						ent->set("corrupted", isCorrupted);
						hasStat = TRUE;
						break;
					}
				}
				if (!hasStat) {
					entry->set("corrupted", isCorrupted);
					statsObject->add(entry);
				}
			}
		}
		delete[] aStatList;
		//}

		// Get items in the sockets
		if (pBuffer->getNumber("sockets") > 0) {
			JSONArray* sockets = new JSONArray();
			for (UnitAny* sItem = pItem->pInventory->pFirstItem; sItem; sItem = sItem->pItemData->pNextInvItem) {
				JSONObject* sock = new JSONObject();
				GetItemInfo(sItem, sock);
				sockets->add(sock);
			}
			pBuffer->set("socketed", sockets);
		}
	}
}

void StashExport::WriteStash() {
	BnetData* pInfo = (*p_D2LAUNCH_BnData);

	if (!Tables::isInitialized()) {
		PrintText(1, "Waiting for MPQ Data to finish loading...");
		return;
	}

	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit) return;

	// Make sure the directory exists
	CreateDirectory((BH::path + "\\stash\\").c_str(), NULL);

	std::string path = BH::path + "\\stash\\" + pInfo->szAccountName + "_" + unit->pPlayerData->szName + ".txt";
	fstream file(path, std::ofstream::out | std::ofstream::trunc);
	if (!file.is_open()) {
		PrintText(1, "Failed to open %s for writing", path.c_str());
		return;
	}

	auto data = new JSONArray;
	std::map<JSONObject*, unsigned int> objCounts;
	for (UnitAny* pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->NodePage == NODEPAGE_EQUIP &&
			!Toggles["Include Equipment"].state) {
			continue;
		}

		auto stash = new JSONObject();
		GetItemInfo(pItem, stash);
		auto name = stash->getString("type");
		if (name.find("Potion") == std::string::npos
			&& name.find("Scroll") == std::string::npos
			&& name.find("Tome") == std::string::npos) { // skip potions & scrolls in the output

			auto item = (JSONObject*)data->contains(stash);
			if (item) {
				// Item already found, track the count
				if (objCounts[item]) {
					objCounts[item]++;
				}
				else {
					objCounts[item] = 2u;
				}
			}
			else {
				data->add(stash);
			}
		}
	}
	for (auto it = objCounts.begin(); it != objCounts.end(); it++) {
		it->first->set("count", (int)it->second);
	}

	std::string buffer;
	if (exportType == 0 /* text */) {
		JSONWriter writer(buffer, SER_OPT_FORMATTED);
		data->serialize(writer);
	}
	else { /* text */
		std::string tmpltName = options.at(exportType);
		if (MustacheTemplates.find(tmpltName) != MustacheTemplates.end()) {
			buffer = Mustache::renderTemplate(MustacheTemplates[tmpltName].get(), Mustache::Context(data, [](std::string name) -> Mustache::AMustacheTemplate* {
				auto tmpl = MustacheTemplates.find(name);
				if (tmpl != MustacheTemplates.end()) {
					return tmpl->second.get();
				}

				return nullptr;
				}));
		}
	}

	file.write(buffer.c_str(), buffer.length());
	delete data;
	PrintText(White, "Exported stash to: %s", path.c_str());
}

void StashExport::CopyItemJSONToClipboard() {
	if (!Tables::isInitialized()) {
		PrintText(1, "Waiting for MPQ Data to finish loading...");
		return;
	}

	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit) return;

	//UnitAny* pItem = D2CLIENT_GetCursorItem();
	UnitAny* pItem = (*p_D2CLIENT_HoverItem);
	if (!pItem) return;

	auto stash = new JSONObject();
	bool bCondenseStats = Toggles["Condense Stats"].state;
	bool bIncludeEquipment = Toggles["Include Equipment"].state;
	bool bIncludeFixedStats = Toggles["Include Fixed Stats"].state;

	Toggles["Condense Stats"].state = false;
	Toggles["Include Equipment"].state = true;
	Toggles["Include Fixed Stats"].state = true;
	GetItemInfo(pItem, stash);
	Toggles["Condense Stats"].state = bCondenseStats;
	Toggles["Include Equipment"].state = bIncludeEquipment;
	Toggles["Include Fixed Stats"].state = bIncludeFixedStats;

	std::string buffer;
	JSONWriter writer(buffer, SER_OPT_FORMATTED);
	stash->serialize(writer);

	const char* clipBuffer = buffer.c_str();
	int len = buffer.length() + 1;
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
	memcpy(GlobalLock(hMem), clipBuffer, len);
	GlobalUnlock(hMem);
	OpenClipboard(0);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	CloseClipboard();

	delete stash;
	PrintText(White, "Copied item to clipboard");
}

void StashExport::OnKey(bool up, BYTE key, LPARAM lParam, bool* block) {
	if (key == exportGear) {
		*block = true;
		if (up)
			return;
		WriteStash();
	}
	else if (key == VK_ESCAPE) {
		if (up &&
			StashExport::Toggles["Export On Menu"].state &&
			D2CLIENT_GetUIState(UI_ESCMENU_MAIN)) {
			WriteStash();
		}
	}
	// Control + C
	else if (key == 67 && GetKeyState(VK_CONTROL) & 0x8000) {
		*block = true;
		if (!up)
			return;
		CopyItemJSONToClipboard();
	}
	for (map<string, Toggle>::iterator it = Toggles.begin(); it != Toggles.end(); it++) {
		if (key == (*it).second.toggle) {
			*block = true;
			if (up) {
				(*it).second.state = !(*it).second.state;
			}
			return;
		}
	}
}

static JSONObject* INVALID_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	return nullptr;
}

static JSONObject* DEFAULT_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	auto id2 = param ? param->toInt() : 0;
	int statId = (int)skDef->getNumber("ID");
	StatList* pStatList = D2COMMON_GetStatList(pItem, 0, STAT_MAX);
	int statVal = D2COMMON_GetStatValueFromStatList(pStatList, statId, 0);
	//int statVal = D2COMMON_GetUnitStat(pItem, (int)skDef->getNumber("ID"), id2);
	if (statVal) {
		auto entry = StashExport::getStatEntry(
			(int)skDef->getNumber("ID"),
			id2,
			statVal,
			min,
			max);
		return entry;
	}

	return nullptr;
}

static JSONObject* DAMAGE_PERCENT_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	auto entry = new JSONObject();
	std::string name = "dmg%";
	StatList* pStatList = D2COMMON_GetStatList(pItem, 0x00, STAT_MAX);
	StatList* pStatList2 = D2COMMON_GetStatList(pItem, 0xAB, STAT_MAX);
	int statVal1 = D2COMMON_GetStatValueFromStatList(pStatList, STAT_ENHANCEDMINIMUMDAMAGE, 0);
	int statVal2 = D2COMMON_GetStatValueFromStatList(pStatList2, STAT_ENHANCEDMINIMUMDAMAGE, 0);
	int statVal = statVal1 + statVal2;
	entry->set("name", name);
	entry->set("value", statVal);
	auto range = new JSONObject();
	range->set("min", (int)min);
	range->set("max", (int)max);
	entry->set("range", range);
	return entry;
}

static JSONObject* DAMAGE_RANGE_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	if (StashExport::Toggles["Include Fixed Stats"].state) {
		auto entry = new JSONObject();
		entry->set("name", NAMEOF((int)skDef->getNumber("ID")));
		entry->set("min", min);
		entry->set("max", max);
		return entry;
	}

	return nullptr;
}

static JSONObject* SKILL_ON_X_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	if (StashExport::Toggles["Include Fixed Stats"].state) {
		auto entry = new JSONObject();
		std::string skill;
		auto id = param ? param->toInt() : 0;
		if (id) {
			auto sk = Tables::Skills.binarySearch("Id", id);
			if (sk) {
				skill = sk->getString("skill");
			}
		}
		else {
			skill = param->toString();
		}

		entry->set("name", NAMEOF((int)skDef->getNumber("ID")));
		entry->set("skill", skill);
		entry->set("chance%", min);
		entry->set("level", max);
		return entry;
	}

	return nullptr;
}

static JSONObject* CLASS_SKILL_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	return DEFAULT_FUNCTION(pItem, skProp, skDef, skProp->get("val1"), min, max);
}

static JSONObject* SKILL_TAB_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	auto id2 = param ? param->toInt() : 0;
	auto elem = JSONNumber(id2);
	return DEFAULT_FUNCTION(pItem, skProp, skDef, &elem, min, max);
}

static JSONObject* SKILL_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	auto id = param ? param->toInt() : 0;
	if (!id) {
		auto sk = Tables::Skills.findEntry("skill", param->toString());
		if (sk) id = (int)sk->getNumber("Id");
	}

	auto elem = JSONNumber(id);
	return DEFAULT_FUNCTION(pItem, skProp, skDef, &elem, min, max);
}

static JSONObject* CHARGED_FUNCTION(UnitAny* pItem, JSONObject* skProp, JSONObject* skDef, JSONElement* param, int min, int max) {
	if (StashExport::Toggles["Include Fixed Stats"].state) {
		auto entry = new JSONObject();
		std::string skill;
		auto id = param ? param->toInt() : 0;
		if (id) {
			auto sk = Tables::Skills.binarySearch("Id", id);
			if (sk) {
				skill = sk->getString("skill");
			}
		}
		else {
			skill = param->toString();
		}

		entry->set("name", NAMEOF((int)skDef->getNumber("ID")));
		entry->set("skill", skill);
		entry->set("charges", min);
		entry->set("level", max);
		return entry;
	}

	return nullptr;
}

std::function<JSONObject* (UnitAny*, JSONObject*, JSONObject*, JSONElement*, int, int)> STAT_FUNCTIONS[] = {
	&INVALID_FUNCTION,	// Invalid function
	&DEFAULT_FUNCTION,	// Func 1
	&DEFAULT_FUNCTION,	// Func 2
	&DEFAULT_FUNCTION,	// Func 3
	&DEFAULT_FUNCTION,	// Func 4
	&DEFAULT_FUNCTION,	// Func 5
	&DEFAULT_FUNCTION,	// Func 6
	&DAMAGE_PERCENT_FUNCTION,	// Func 7
	&DEFAULT_FUNCTION,	// Func 8
	&DEFAULT_FUNCTION,	// Func 9
	&SKILL_TAB_FUNCTION,	// Func 10
	&SKILL_ON_X_FUNCTION,	// Func 11
	&DEFAULT_FUNCTION,	// Func 12
	&DEFAULT_FUNCTION,	// Func 13
	&DEFAULT_FUNCTION,	// Func 14
	&DAMAGE_RANGE_FUNCTION,	// Func 15
	&DEFAULT_FUNCTION,	// Func 16
	&DEFAULT_FUNCTION,	// Func 17
	&DEFAULT_FUNCTION,	// Func 18
	&CHARGED_FUNCTION,	// Func 19
	&DEFAULT_FUNCTION,	// Func 20
	&CLASS_SKILL_FUNCTION,	// Func 21
	&SKILL_FUNCTION
};

char* QUALITY_NAMES[] = {
	"None",
	"Inferior",
	"Normal",
	"Superior",
	"Magic",
	"Set",
	"Rare",
	"Unique",
	"Crafted"
};

IdNamePair ITEM_LOCATIONS[6] = {
	{ STORAGE_INVENTORY, "Inventory" },
	{ STORAGE_EQUIP, "Equip" },
	{ STORAGE_BELT, "Belt" },
	{ STORAGE_CUBE, "Cube" },
	{ STORAGE_STASH, "Stash" },
	{ STORAGE_NULL, "Null" }
};

IdNamePair NODE_PAGES[4] = {
	{ 0, "Unknown" },
	{ NODEPAGE_STORAGE, "Storage" },
	{ NODEPAGE_BELTSLOTS, "Beltslots" },
	{ NODEPAGE_EQUIP, "Equiped" }
};

IdNamePair CLASS_NAMES[7] = {
	{ 0, "Amazon Skills" },
	{ 1, "Sorceress Skills" },
	{ 2, "Necromancer Skills" },
	{ 3, "Paladin Skills" },
	{ 4, "Barbarian Skills" },
	{ 5, "Druid Skills" },
	{ 6, "Assassin Skills" }
};

IdNamePair SKILL_TABS[21] = {
	{ 0, "Bow and Crossbow Skills (Amazon)" },
	{ 1, "Passive and Magic Skills (Amazon)" },
	{ 2, "Javalin and Spear Skills (Amazon)" },
	{ 8, "Fire Skills (Sorceress)" },
	{ 9, "Lightning Skills (Sorceress)" },
	{ 10, "Cold Skills (Sorceress)" },
	{ 16, "Curses Skills (Necromancer)" },
	{ 17, "Poison and Bone Skills (Necromancer)" },
	{ 18, "Summoning Skills (Necromancer)" },
	{ 24, "Combat Skills (Paladin)" },
	{ 25, "Offensive Auras (Paladin)" },
	{ 26, "Defensive Auras (Paladin)" },
	{ 32, "Combat Skills (Barbarian)" },
	{ 33, "Masteries (Barbarian)" },
	{ 34, "Warcries (Barbarian)" },
	{ 40, "Summoning (Druid)" },
	{ 41, "Shapeshifting (Druid)" },
	{ 42, "Elemental (Druid)" },
	{ 48, "Traps (Assassin)" },
	{ 49, "Shadow Desciplines (Assassin)" },
	{ 50, "Martial Arts (Assassin)" },
};
