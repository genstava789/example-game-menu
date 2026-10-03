#include <list>
#include <vector>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"

//Target lib here
#define targetLibName OBFUSCATE("libil2cpp.so")

#include "Includes/Macros.h"

#include <signal.h>
#include <ucontext.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

// Native Crash Handler: captures SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT
void write_native_crash_log(const char *msg) {
    const char *paths[] = {
        "/storage/emulated/0/Documents/crash_log.txt",
        "/sdcard/Documents/crash_log.txt",
        "/sdcard/crash_log.txt",
        "/storage/emulated/0/Download/crash_log.txt"
    };

    for (int i = 0; i < 4; i++) {
        int fd = open(paths[i], O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            write(fd, msg, strlen(msg));
            close(fd);
        }
    }
}

void native_signal_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    ucontext_t *uc = (ucontext_t *)ucontext_raw;
    char buffer[1024];

    uintptr_t pc = 0;
    uintptr_t lr = 0;
#if defined(__aarch64__)
    if (uc) {
        pc = uc->uc_mcontext.pc;
        lr = uc->uc_mcontext.regs[30];
    }
#elif defined(__arm__)
    if (uc) {
        pc = uc->uc_mcontext.arm_pc;
        lr = uc->uc_mcontext.arm_lr;
    }
#endif

    Dl_info dlinfo;
    const char *libName = "Unknown";
    uintptr_t relPC = 0;
    if (dladdr((void *)pc, &dlinfo) && dlinfo.dli_fname) {
        libName = dlinfo.dli_fname;
        relPC = pc - (uintptr_t)dlinfo.dli_fbase;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    snprintf(buffer, sizeof(buffer),
        "\n==================== NATIVE CRASH DETECTED ====================\n"
        "Time: %s\n"
        "Signal: %d (%s)\n"
        "Faulting Address: %p\n"
        "PC: %p (Library: %s + 0x%lx)\n"
        "LR: %p\n"
        "===============================================================\n",
        time_str,
        sig,
        (sig == SIGSEGV ? "SIGSEGV (Segmentation Fault)" :
         sig == SIGBUS  ? "SIGBUS (Bus Error)" :
         sig == SIGFPE  ? "SIGFPE (Arithmetic Exception)" :
         sig == SIGILL  ? "SIGILL (Illegal Instruction)" :
         sig == SIGABRT ? "SIGABRT (Abort)" : "OTHER"),
        info ? info->si_addr : NULL,
        (void *)pc,
        libName,
        (unsigned long)relPC,
        (void *)lr
    );

    LOGE("%s", buffer);
    write_native_crash_log(buffer);

    signal(sig, SIG_DFL);
    raise(sig);
}

void install_crash_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = native_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// Mod feature variables
bool addCoin = false;
int coinAmount = 50000;
int speedMultiplier = 1;
int jumpMultiplier = 1;
int keysBonus = 0;
bool needKeyVisualRefresh = false;
bool magnetEnabled = false;
bool magnetNeedsSync = true;

void *activeMagnetizer = NULL;
void *activeKeyCounter = NULL;

// Forward declarations of trampolines
void (*old_AddCoins)(void *instance, int coins);
void (*old_AddKeys)(void *instance, int keys);
void (*old_AddCurrency)(void *instance, int type, int amount, int origin, int64_t expiration, void *method);
int (*old_GetCurrency)(void *instance, int type);
void (*old_CurrencyCounter_OnEnable)(void *instance);
void (*old_CurrencyCounter_OnDisable)(void *instance);
void (*old_SetDirectly)(void *instance);
float (*old_get_SpeedModifier)(void *instance);
void (*old_ForceRelativeJump)(void *instance, float height, bool userTriggered);
void (*old_Magnetizer_OnEnable)(void *instance);
void (*old_Magnetizer_OnDisable)(void *instance);
void (*old_Magnetizer_RefreshState)(void *instance);
bool (*old_CanBeMagnetizedByUser)(void *instance, void *characterInfo);

// Feature 1: Add Coins & Keys Hooks
// Hook for RunSessionData.AddCoins(int coins) - RVA 0x1F8E1B4
void AddCoins(void *instance, int coins) {
    if (instance != NULL) {
        if (addCoin) {
            coins += coinAmount;
        }
        if (keysBonus > 0 && old_AddKeys != NULL) {
            old_AddKeys(instance, keysBonus);
            keysBonus = 0;
        }
    }
    return old_AddCoins(instance, coins);
}

// Hook for RunSessionData.AddKeys(int keys) - RVA 0x1F8E1DC
void AddKeys(void *instance, int keys) {
    if (instance != NULL && keysBonus > 0) {
        keys += keysBonus;
        keysBonus = 0;
    }
    return old_AddKeys(instance, keys);
}

// Hook for WalletModel.AddCurrency(int type, int amount, int origin, int64_t expiration, MethodInfo* method) - RVA 0x3D92400
void AddCurrency(void *instance, int type, int amount, int origin, int64_t expiration, void *method) {
    if (instance != NULL) {
        if (type == 1 && addCoin) { // 1 = CurrencyType.Coins
            amount += coinAmount;
        }
        if (type == 2 && keysBonus > 0) { // 2 = CurrencyType.Keys
            amount += keysBonus;
            keysBonus = 0;
        }
    }
    return old_AddCurrency(instance, type, amount, origin, expiration, method);
}

// Hook for FlyingSpriteCounter.SetDirectly() - RVA 0x1FA3F98
void SetDirectly(void *instance) {
    return old_SetDirectly(instance);
}

// Hook for WalletModel.GetCurrency(int type) - RVA 0x3D92900
int GetCurrency(void *instance, int type) {
    if (needKeyVisualRefresh && activeKeyCounter != NULL && old_SetDirectly != NULL) {
        needKeyVisualRefresh = false;
        old_SetDirectly(activeKeyCounter);
    }
    int orig = old_GetCurrency(instance, type);
    if (instance != NULL && type == 2 && keysBonus > 0) {
        return orig + keysBonus;
    }
    return orig;
}

// Hook for CurrencyCounter.OnEnable() - RVA 0x1FA370C
void CurrencyCounter_OnEnable(void *instance) {
    old_CurrencyCounter_OnEnable(instance);
    if (instance != NULL) {
        int currType = *(int *)((uintptr_t)instance + 0x80);
        if (currType == 2) { // 2 = CurrencyType.Keys
            activeKeyCounter = instance;
            if (keysBonus > 0 && old_SetDirectly != NULL) {
                old_SetDirectly(instance);
            }
        }
    }
}

// Hook for CurrencyCounter.OnDisable() - RVA 0x1FA38F8
void CurrencyCounter_OnDisable(void *instance) {
    if (activeKeyCounter == instance) {
        activeKeyCounter = NULL;
    }
    old_CurrencyCounter_OnDisable(instance);
}

// Hook for Magnetizer.RefreshState() - RVA 0x2566D88
void Magnetizer_RefreshState(void *instance) {
    if (instance != NULL) {
        bool isSneakers = *(bool *)((uintptr_t)instance + 0x50);
        if (!isSneakers && magnetEnabled) {
            void *ownersList = *(void **)((uintptr_t)instance + 0x58);
            if (ownersList != NULL) {
                int originalSize = *(int *)((uintptr_t)ownersList + 0x18);
                *(int *)((uintptr_t)ownersList + 0x18) = 1;
                old_Magnetizer_RefreshState(instance);
                *(int *)((uintptr_t)ownersList + 0x18) = originalSize;
                return;
            }
        }
    }
    old_Magnetizer_RefreshState(instance);
}

// Feature 2: Speed Multiplier Hook
// Hook for CharacterMotor.get_SpeedModifier() - RVA 0x1F1A3A0
float get_SpeedModifier(void *instance) {
    if (instance != NULL) {
        // Real-time key visual refresh if pending
        if (needKeyVisualRefresh && activeKeyCounter != NULL && old_SetDirectly != NULL) {
            needKeyVisualRefresh = false;
            old_SetDirectly(activeKeyCounter);
        }

        // Automatic magnet synchronization on game start/restart
        if (magnetNeedsSync && activeMagnetizer != NULL && old_Magnetizer_RefreshState != NULL) {
            magnetNeedsSync = false;
            Magnetizer_RefreshState(activeMagnetizer);
        }

        // Full direct speed multiplier
        if (speedMultiplier > 1) {
            return old_get_SpeedModifier(instance) * (float)speedMultiplier;
        }
    }
    return old_get_SpeedModifier(instance);
}

// Feature 3: Jump Multiplier Hook (ForceRelativeJump with natural scaling) - RVA 0x1F14424
void ForceRelativeJump(void *instance, float height, bool userTriggered) {
    if (instance != NULL && jumpMultiplier > 1) {
        float mult = 1.0f + (float)(jumpMultiplier - 1) * 0.2f;
        height *= mult;
    }
    return old_ForceRelativeJump(instance, height, userTriggered);
}

// Feature 4: Magnet Hooks
// Hook for Magnetizer.OnEnable() - RVA 0x2566AC8
void Magnetizer_OnEnable(void *instance) {
    old_Magnetizer_OnEnable(instance);
    if (instance != NULL) {
        bool isSneakers = *(bool *)((uintptr_t)instance + 0x50);
        if (!isSneakers) {
            activeMagnetizer = instance;
            magnetNeedsSync = true;
        }
    }
}

// Hook for Magnetizer.OnDisable() - RVA 0x2566BC8
void Magnetizer_OnDisable(void *instance) {
    if (activeMagnetizer == instance) {
        activeMagnetizer = NULL;
    }
    magnetNeedsSync = true;
    old_Magnetizer_OnDisable(instance);
}

// Hook for Magnetizable.CanBeMagnetizedByUser(CharacterInfo characterInfo) - RVA 0x25666F4
bool CanBeMagnetizedByUser(void *instance, void *characterInfo) {
    if (magnetEnabled) {
        return true;
    }
    return old_CanBeMagnetizedByUser(instance, characterInfo);
}

// Thread to initialize hooks
void *hack_thread(void *) {
    LOGI(OBFUSCATE("pthread created"));

    //Check if target lib is loaded
    do {
        sleep(1);
    } while (!isLibraryLoaded(targetLibName));

    LOGI(OBFUSCATE("%s has been loaded"), (const char *) targetLibName);

#if defined(__aarch64__)

    // 1. Add Coin & Key Hooks
    HOOK("0x1F8E1B4", AddCoins, old_AddCoins);
    HOOK("0x1F8E1DC", AddKeys, old_AddKeys);
    HOOK("0x3D92400", AddCurrency, old_AddCurrency);
    HOOK("0x3D92900", GetCurrency, old_GetCurrency);
    HOOK("0x1FA370C", CurrencyCounter_OnEnable, old_CurrencyCounter_OnEnable);
    HOOK("0x1FA38F8", CurrencyCounter_OnDisable, old_CurrencyCounter_OnDisable);
    HOOK("0x1FA3F98", SetDirectly, old_SetDirectly);

    // 2. Speed Multiplier Hook
    HOOK("0x1F1A3A0", get_SpeedModifier, old_get_SpeedModifier);

    // 3. Jump Multiplier Hook (ForceRelativeJump with natural scaling)
    HOOK("0x1F14424", ForceRelativeJump, old_ForceRelativeJump);

    // 4. Magnet Hooks
    HOOK("0x2566AC8", Magnetizer_OnEnable, old_Magnetizer_OnEnable);
    HOOK("0x2566BC8", Magnetizer_OnDisable, old_Magnetizer_OnDisable);
    HOOK("0x2566D88", Magnetizer_RefreshState, old_Magnetizer_RefreshState);
    HOOK("0x25666F4", CanBeMagnetizedByUser, old_CanBeMagnetizedByUser);

#else 

    HOOK("0x1F8E1B4", AddCoins, old_AddCoins);
    HOOK("0x1F8E1DC", AddKeys, old_AddKeys);
    HOOK("0x3D92400", AddCurrency, old_AddCurrency);
    HOOK("0x3D92900", GetCurrency, old_GetCurrency);
    HOOK("0x1FA370C", CurrencyCounter_OnEnable, old_CurrencyCounter_OnEnable);
    HOOK("0x1FA38F8", CurrencyCounter_OnDisable, old_CurrencyCounter_OnDisable);
    HOOK("0x1FA3F98", SetDirectly, old_SetDirectly);
    HOOK("0x1F1A3A0", get_SpeedModifier, old_get_SpeedModifier);
    HOOK("0x1F14424", ForceRelativeJump, old_ForceRelativeJump);
    HOOK("0x2566AC8", Magnetizer_OnEnable, old_Magnetizer_OnEnable);
    HOOK("0x2566BC8", Magnetizer_OnDisable, old_Magnetizer_OnDisable);
    HOOK("0x2566D88", Magnetizer_RefreshState, old_Magnetizer_RefreshState);
    HOOK("0x25666F4", CanBeMagnetizedByUser, old_CanBeMagnetizedByUser);

#endif

    return NULL;
}

// Mod Menu Feature List
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Toggle_Add coin (50000)"),      // case 0
            OBFUSCATE("SeekBar_Speed Multiplier_1_50"),// case 1
            OBFUSCATE("SeekBar_Jump Multiplier_1_10"), // case 2 (natural scaling)
            OBFUSCATE("InputValue_1000000_Add Keys"),  // case 3 (manual input with Apply button)
            OBFUSCATE("Toggle_Magnet")                 // case 4 (magnet item active)
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return (ret);
}

void Changes(JNIEnv *env, jclass clazz, jobject obj,
                                        jint featNum, jstring featName, jint value,
                                        jboolean boolean, jstring str) {

    LOGD(OBFUSCATE("Feature name: %d - %s | Value: = %d | Bool: = %d | Text: = %s"), featNum,
         env->GetStringUTFChars(featName, 0), value,
         boolean, str != NULL ? env->GetStringUTFChars(str, 0) : "");

    switch (featNum) {
        case 0: // Add coin (50000)
            addCoin = boolean;
            break;
        case 1: // Speed Multiplier
            speedMultiplier = value;
            break;
        case 2: // Jump Multiplier
            jumpMultiplier = value;
            break;
        case 3: // Add Keys (manual input with Apply button)
            if (value > 0) {
                keysBonus += value;
                needKeyVisualRefresh = true;
            }
            break;
        case 4: // Magnet Toggle
            magnetEnabled = boolean;
            magnetNeedsSync = true;
            break;
    }
}

__attribute__((constructor))
void lib_main() {
    install_crash_handler();
    // Create a new thread so it does not block the main thread, means the game would not freeze
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
}

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),  OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),  OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),  OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(GetFeatureList)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterMain(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("CheckOverlayPermission"), OBFUSCATE("(Landroid/content/Context;)V"), reinterpret_cast<void *>(CheckOverlayPermission)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Main"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;

    return JNI_OK;
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (RegisterMenu(env) != 0)
        return JNI_ERR;
    if (RegisterPreferences(env) != 0)
        return JNI_ERR;
    if (RegisterMain(env) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
