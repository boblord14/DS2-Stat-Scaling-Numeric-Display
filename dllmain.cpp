//
// Created by false on 6/2/2024.
//
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cwchar>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include "Signature.h"
#include "dinput8/dinputWrapper.h"

// Export DINPUT8
tDirectInput8Create oDirectInput8Create;

constexpr int DIGIT_LENGTH_MAX = 4;
uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));

typedef uint32_t* (__fastcall* setStatFunction)(uint64_t, uint32_t*, wchar_t*, uint64_t, int, char, uint32_t, uint32_t);
uintptr_t statFunctionAddr = baseAddress + 0x37720;
static setStatFunction originalFunctionAddr = nullptr;

typedef float (__fastcall* getScalingValue)(uint64_t, uint32_t);
uintptr_t scalingValueAddr = baseAddress + 0x32460;
auto scalingValue = reinterpret_cast<getScalingValue>(scalingValueAddr);

typedef uint32_t* (__fastcall* fillStatEntryStr)(uint32_t*, uint32_t, float, const wchar_t*);
uintptr_t statEntryStrAddr = baseAddress + 0x3d6c0;
auto statEntryStr = reinterpret_cast<fillStatEntryStr>(statEntryStrAddr);

typedef uint8_t* (__fastcall* getParamRowById)(void*, uint32_t);
uintptr_t paramRowLookupAddr = baseAddress + 0x3E8F0;
auto paramRowLookup = reinterpret_cast<getParamRowById>(paramRowLookupAddr);

static uintptr_t* gameManagerImpAddr = nullptr;
constexpr uintptr_t WEAPON_PARAM = 0x420;
constexpr uintptr_t WEAPON_REINFORCE_ID = 0x8;

constexpr uintptr_t WEAPON_REINFORCE_PARAM = 0x470;
constexpr uintptr_t DAMAGE_MULT_PHYSICAL = 0xA0;
constexpr uintptr_t INFUSION_MULT_MAGIC = 0xC4;

constexpr uintptr_t SELF_STRUCT_WEAPON_INFUSION = 0xA7;
constexpr uintptr_t SELF_STRUCT_WEAPON_ID = 0x7C;

constexpr int ELEMENT_COUNT = 7; // phys, magic, lightning, fire, dark, poison, bleed IN THAT ORDER

static void createAndLoadRealDinput8() {
    /**
     * Passthrough for the real dinput8.dll
     * Borrowed from the og modengine.
     */
    wchar_t dllPath[MAX_PATH];

    GetSystemDirectoryW(dllPath, MAX_PATH);
    lstrcatW(dllPath, L"\\dinput8.dll");
    HMODULE hMod = LoadLibraryW(dllPath);
    oDirectInput8Create = (tDirectInput8Create)GetProcAddress(hMod, "DirectInput8Create");
}

int infusionSlotConversion(const uint8_t infusion) {
    /**
     * Determining valid infusions to actually analyze, and converting them to the ordering used in params from statID values
     *
     * @param infusion  The infusion value directly from self object in the function hook
     * @return the infusion id(useful for finding slots) if valid, -1 otherwise
     */
    switch (infusion) {
        case 1: return 3; // fire
        case 2: return 1; // magic
        case 3: return 2; // lightning
        case 4: return 4; // dark
        case 5: return 5; // poison
        case 6: return 6; // bleed
        default: return -1; // raw, mundane, enchanted infusions(irrelevant, treat same as phys)
    }
}

bool InitHelper() {
    /**
     * Setup gameMgrImp off of AOB
     *
     * @return  True if gameManagerImpAddr succesfully initialized, false otherwise
     */
    try {
        ModuleData ds2Data("DarkSoulsII.exe");
        auto gameManagerImpSig = Signature("48 8B 05 ?? ?? ?? ?? 48 8B 58 38 48 85 DB  74  ?? F6");
        gameManagerImpAddr = static_cast<uintptr_t*>(gameManagerImpSig.Scan(&ds2Data, 0x3, 0x7));
    } catch (const std::exception& e) {
        gameManagerImpAddr = nullptr;
    }

    return gameManagerImpAddr != nullptr;
}

uint8_t* getParamRow(uintptr_t paramTableTarget, uint32_t rowId) {
    /**
     * A more manual method to lookup param data from the param db, without having to reinvent the wheel and make an impl
     * of PPV2 in cpp. Still rather annoying because most param retrieval code is obfuscated so I'm forced to do this manually.
     *
     * @param paramTableTarget  The offset of the param table, derived from the ce "get param row from ID" functions
     * @param rowId  The standard param row ID of the param entry you want to look up
     * @return A pointer to the param row desired
     */

    //manual pointerchain because I'm not gonna figure out that library again right now
    if (!gameManagerImpAddr) return nullptr;

    // [[[GameManagerImp]+18]+paramTableTarget]+C8
    const uintptr_t gameManagerImp = *gameManagerImpAddr;
    if (!gameManagerImp) return nullptr;

    const uintptr_t offset1 = *reinterpret_cast<uintptr_t*>(gameManagerImp + 0x18);
    if (!offset1) return nullptr;

    const uintptr_t offset2 = *reinterpret_cast<uintptr_t*>(offset1 + paramTableTarget);
    if (!offset2) return nullptr;

    const auto addr = reinterpret_cast<void *>(offset2 + 0xC8);

    return paramRowLookup(addr, rowId);

}

bool loadInfusionDataFromParams(const uint32_t weaponId, const int element, float damage_mults[ELEMENT_COUNT]) {
    /**
     * Manual calculations for how infusions modify scaling on a weapon. Performs param lookups and does the math.
     * Handles how innate elements interact with infusion elements and the scaling effects on the weapon that results.
     *
     * It's a little confusing, but the infusion multiplier(in params) divided by the number of innate elemental damage types
     * that aren't the infusion element(aux counts) will produce a value. This number is subtracted by the damage types that aren't
     * the weapon's infusion damage type.
     *
     * Then the weapon infusion multiplier(in params, specific to each infusion) is multiplied by the infusion element type's scaling
     * to get the scaling value for the relevant damage type.
     *
     * Whoever came up with this needs to be fired, this math is really really suspect icl.
     *
     * @param weaponId  The WeaponParam weapon id for the weapon in question
     * @param element  The element as an int, converted through the infusionSlotConversion to convert it to the param ordering for the elements
     * @param damage_mults  Simple array that holds the final multiplier values(or will, at least). Starts empty.
     * @return  True if successful, false if anything goes wrong and can't be found
     */
    uint8_t* weaponParamRow = getParamRow(WEAPON_PARAM, weaponId);
    if (!weaponParamRow) return false;

    const uint32_t weapon_reinforce_id = *reinterpret_cast<uint32_t*>(weaponParamRow + WEAPON_REINFORCE_ID);
    uint8_t* weaponReinforceParamRow = getParamRow(WEAPON_REINFORCE_PARAM, weapon_reinforce_id);
    if (!weaponReinforceParamRow) return false;

    for (int i = 0; i < ELEMENT_COUNT; i++) {
        damage_mults[i] = *reinterpret_cast<float*>(weaponReinforceParamRow + (DAMAGE_MULT_PHYSICAL + i * 0x4));
    }

    const float specificInfusionMult = *reinterpret_cast<float*>(weaponReinforceParamRow + (INFUSION_MULT_MAGIC + (element - 1) * 0x4));

    // get number of innate elements
    int innateElementCount = 0;
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        if (i != element && damage_mults[i] > 0.0f) {
            innateElementCount++;
        }
    }

    // subtraction for innate elements based on infused elements, only on elements not part of the infusion
    // i hate dark souls 2 scaling who came up with the math here
    if (innateElementCount > 0) {
        const float nonInfusedElementLoss = specificInfusionMult / static_cast<float>(innateElementCount);
        for (int i = 0; i < ELEMENT_COUNT; i++) {
            if (i != element && damage_mults[i] > 0.0f) {
                damage_mults[i] = fmaxf(damage_mults[i] - nonInfusedElementLoss, 0.0f);
            }
        }
    }

    // infusion element gets the mult addition
    damage_mults[element] += specificInfusionMult;

    return true;

}

float calculateInfusionScalingMult(const uint64_t self, const uint32_t statID, const float scalingDecimal) {
    /**
     * Function that pulls the data from our input structs/values to calculate the infusion scaling.
     * self is a struct that has some data related to our weapon's properties, we can get the infusion and item ID from it.
     *
     * Function automatically built to ignore nonelemental infusions(raw, mundane, enchanted, uninfused) as those
     * don't do anything with weird multipliers. Runs it through the calculator to get the scaling multiplier and then
     * multiplies the actual scaling value by the multiplier for the real scaling value.
     *
     * @param self  The self struct passed in by the function hook from the original function(param_1 by default)
     * @param statID The statID value passed in by the function hook from the original function(param_7 by default)
     * @param scalingDecimal The raw unmultiplied scaling value. Returned from calling scalingValue
     * @return The final scaling value
     */

    // limit to specific relevant scaling stats
    if (statID < 0x29 || statID > 0x2E) {
        return scalingDecimal;
    }

    const uint8_t infusion = *reinterpret_cast<const uint8_t *>(self + SELF_STRUCT_WEAPON_INFUSION);

    const int infusionElement = infusionSlotConversion(infusion);
    if (infusionElement == -1) return scalingDecimal; // irrelevant infusion

    const uint32_t weaponId = *reinterpret_cast<const uint32_t *>(self + SELF_STRUCT_WEAPON_ID);

    float damageMults[ELEMENT_COUNT];

    if (!loadInfusionDataFromParams(weaponId, infusionElement, damageMults)) return scalingDecimal;

    int infusionSlot;
    if (statID == 0x29 || statID == 0x2A) {
        // both str and dex work on the same slot, so manual override from the below calc
        infusionSlot = 0;
    }else {
        // infusion slot can be derived from the statID value here as they're in the same order
        infusionSlot = static_cast<int>(statID) - 0x2A;
    }
    return scalingDecimal * (damageMults[infusionSlot] / 100.0f);
}

int scalingPercentCleanup(const float scalingDecimal) {
    /**
     * Scaling is done weirdly, and this isn't perfect and still has off-by-one errors on some occasions. Not sure if the
     * scaling calculator google sheet that later became the wikigg is wrong, or if my maths are wrong somewhere.
     *
     * The biggest thing here is we FLOOR values, not round. Adds some bounds checking too despite the fact we shouldn't
     * be running into those ever. Might as well be safe.
     *
     * This cleans the value up for display, so don't use it until the calculations are done
     *
     * @param  scalingDecimal The final scalingDecimal value, after infusion multipliers
     * @return The cleaned up scaling value as an integer
     */
    int percentConv = static_cast<int>(floorf(scalingDecimal * 100.0f + 0.001f));

    if (percentConv < 0) percentConv = 0;
    if (percentConv > 999) percentConv = 999;

    return percentConv;
}

uint32_t* __fastcall statFunctionDetour(uint64_t self, uint32_t* output, wchar_t* charBuffer, uint64_t bufferCharLength,
                 int decimal, char asFloat, uint32_t statID, uint32_t flags) {
    /**
     * This is the main stat scaling function that calculates the evil letters of doom and despair and loads them into a string
     * to be displayed. It does a bunch of other things too that I don't care about, so I've recreated the branching paths
     * that lead to the pieces that matter. I'm not going to go into detail on what they do because I really don't know myself,
     * but I know what I need to modify and I can copy paste logic from ghidra to get there.
     *
     * Anyhow we validate a bunch of conditions, that would be necessary to occur before we do the stat scaling calculations.
     * One more check of my own variety(the first one) that ensures that the buffer we're being given is big enough to fit
     * what we want to output. I think it's irrelevant as I'm 99% sure it would get killed by one of the IFs, but I might as
     * well just write good code.
     *
     * Something of note, the IF that does (statID >= 0x29 && statID <= 0x2E) limits it to weapons only. statID == 0x10
     * is the code for armor scaling, so that's included here as well.
     *
     * If any of the IFs fail, we just call the function normally and it does whatever it's supposed to do with them.
     *
     * Once we know we actually are cleared to calculate, we call scalingValue which is a ds2 function called normally
     * in this process, but manually here. Anyhow what it does is take in the self object and the stat ID and return
     * the scaling decimal from params/other calculations.
     *
     * This is pretty much just good as it stands for phys only weapons with a little cleanup, but infusions have more
     * shenanigans involved(real scaling not shown in menu, so manually calculated via calculateInfusionScalingMult.
     *
     * From here we just wipe and clear the buffer, write in our number instead of the letter to the char buffer, and
     * call the statEntryStr function, which loads the output struct with our data.
     *
     * @param self  Struct with object data, useful to get some properties of the weapon out of it
     * @param output  Output struct that's loaded by statEntryStr and returned
     * @param charBuffer  Chars for letter scaling would be written here, but we write numbers instead
     * @param bufferCharLength  Self explanatory, we want ~4 chars to write to the buffer safely
     * @param decimal  Used in some calculations we don't engage in, irrelevant
     * @param asFloat  Used in some calculations we don't engage in, irrelevant
     * @param statID  Determines which stat is being calculated in our infusion, along with armor scaling among other things
     * @param flags  A series of flags used for some checks, we don't really engage with them except for some IFs
     * @return output param after it's ben loaded by statEntryStr, alternatively original function if some checks fail
     */

    const auto bufLen = static_cast<uint32_t>(bufferCharLength);

    // first ifs here is conditions that must not be true for printing out the scaling value according to the ghidra decomp
    if (*reinterpret_cast<char *>(self + 0xac) != '\0' && charBuffer != nullptr && bufLen != 0) {
        if (bufLen >= DIGIT_LENGTH_MAX) {
            //if this is true, we load up the scaling value at the end of this branch
            if ((static_cast<byte>(flags >> 3) & 1) == 1) {
                // this one returns something else though(other stats?)
                if ((statID - 0x50 & 0xfffffff7) != 0) {
                    // crunching down a switch for statIDs
                    // armor scaling is tied behind statID = 0x10 as well, its in a weird spot for the switch in the decomp
                    if (statID >= 0x29 && statID <= 0x2E || statID == 0x10) {

                        // grab actual scaling decimal value, convert to percentage and cap if need be
                        float scalingDecimal = scalingValue(self, statID);                 // drop 'const'
                        scalingDecimal = calculateInfusionScalingMult(self, statID, scalingDecimal);   // infusion split
                        int scalingPercentage = scalingPercentCleanup(scalingDecimal);


                        // i think this maps to a buffer wipe? ghidra code unclear, but why not? can't hurt
                        if (flags & (1u << 2)) {
                            std::memset(charBuffer, 0, bufLen * sizeof(wchar_t));
                        }

                        // write the characters to our buffer
                        std::swprintf(charBuffer, DIGIT_LENGTH_MAX, L"%d", scalingPercentage);

                        // return it converted to our output
                        return statEntryStr(output, statID, scalingDecimal, charBuffer);
                    }
                }
            }
        }
    }
    // otherwise just do the base default
    return originalFunctionAddr(self, output, charBuffer, bufferCharLength, decimal, asFloat, statID, flags);
}

DWORD WINAPI MainThread(LPVOID lpParam)
{
    /**
     * Main dll thread that sets up the hooks, sets up GameManImp, and exits. Not terribly special.
     */

    if (MH_Initialize() != MH_OK) {
        return 1;
    }

    // make sure we got gameManagerImp going
    if (!InitHelper()) return 1;

    if (MH_CreateHook(reinterpret_cast<void*>(statFunctionAddr), reinterpret_cast<void*>(&statFunctionDetour),
        reinterpret_cast<void**>(&originalFunctionAddr)) != MH_OK) return 1;

    if (MH_EnableHook(reinterpret_cast<void*>(statFunctionAddr)) != MH_OK) return 1;

    return 0;
}


BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    /**
     * Dll injection thread, when attached does the MainThread function, when detached kills the hooks. Nothing special.
     */
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        createAndLoadRealDinput8();
        CreateThread(0, 0, &MainThread, nullptr, 0, nullptr);

    }else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return 1;
}