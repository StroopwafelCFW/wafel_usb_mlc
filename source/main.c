#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wafel/dynamic.h>
#include <wafel/utils.h>
#include <wafel/trampoline.h>
#include <wafel/ios/thread.h>
#include <wafel/patch.h>
#include <wafel/ios/svc.h>
#include <wafel/ios/prsh.h>

#define UMS_PROCESS_IDX 47
#define USBPROC2_PROCESS_IDX 36
#define PROCESS_STRUCT_SIZE 0x58

#define DEVTYPE_MLC 0x5

// boot_state
#define CMPT_RETSTAT0 0x00100000

int (*MCP_IVS_stuff)(void) = (void*)(0x0501c238|1);
int (*MCP_MountWithSubdir)(char*, char*, char *, void*) = (void*) 0x05015c7c;
int (*wait_for_dev)(const char*, u32) = (void*)(0x05016cd8|1);


static bool shutdown_from_hai = false;


static u32 IOS_Process_Struct_ARRAY_050711b0 = 0x050711b0;
int resume_process(u32 pidx){
    int mode=*(int*)0x050b7fc8;
    int flags=*(int*)0x050b7fc4;
    u32 IOS_Process_Struct_ARRAY_050711b0 = 0x050711b0;
    u32 IOS_Process_Struct_base = IOS_Process_Struct_ARRAY_050711b0 + PROCESS_STRUCT_SIZE*pidx;
    int resource_handle_id = *(int*)(IOS_Process_Struct_base + 8);
    *(int*)(IOS_Process_Struct_base + 0x10) = 5;
    *(int*)(IOS_Process_Struct_base + 0x4) = 0xfffffffc;
    int res = iosIpcResume(resource_handle_id, mode, flags);
    debug_printf("Manually resuming Process %d returned %d\n", pidx, res);
    return res;
}

static int suspend_mode, suspend_flags;

int suspend_process(u32 pidx) {
    u32 IOS_Process_Struct_base = IOS_Process_Struct_ARRAY_050711b0 + PROCESS_STRUCT_SIZE*pidx;
    int resource_handle_id = *(int*)(IOS_Process_Struct_base + 8);
    *(int*)(IOS_Process_Struct_base + 0x10) = 5;
    *(int*)(IOS_Process_Struct_base + 0x4) = 0xfffffffc;
    int res = iosIpcSuspend(resource_handle_id, suspend_mode, suspend_flags);
    debug_printf("Manually suspending Process %d returned %d\n", pidx, res);
    return res;
}

void pm_system_resume_hook(trampoline_t_state *regs){
    debug_printf("Calling IOS_ResumeAsync(%u, %p, %p, %u, %u)\n", regs->r[0], regs->r[1], regs->r[2], regs->r[3], regs->stack[0]);
}

void pm_resume_count_hook(trampoline_t_state * regs){
    u32 pidx = regs->r[2];
    char * name = *(char**)(0x050711b0 + PROCESS_STRUCT_SIZE*pidx +0x50);
    debug_printf("Resumed Process %d: %s\n", pidx, name);

    u32 *MCP_PM_mode_init =(u32*)0x0508775c;
    debug_printf("MCP_PM_mode_init: %p\n", *MCP_PM_mode_init);

    if(pidx == 1 && shutdown_from_hai){
        *MCP_PM_mode_init = 0x100000;
        debug_printf("MCP_PM_mode_init set to %p\n", *MCP_PM_mode_init);
    }

    if(pidx == 4 && shutdown_from_hai){
        *MCP_PM_mode_init = 0x8000;
        debug_printf("MCP_PM_mode_init set to %p\n", *MCP_PM_mode_init);
    }

    regs->r[0] = regs->r[2];
}

void pm_resume_offset_hook(trampoline_t_state *regs){
    int just_resumed = regs->r[0];
    debug_printf("resumed %d, r3: %p, r2: %p\n", just_resumed, regs->r[3], regs->r[2]);
}

void pm_suspend_params_hook(trampoline_t_state *regs){
    suspend_mode = regs->r[0];
    suspend_flags = regs->r[1];
}

void pm_suspend_count_hook(trampoline_t_state *regs){
    int pidx = regs->r[2];
    if(pidx == UMS_PROCESS_IDX || pidx == 36){
        debug_printf("Skipping suspend of process %d\n", regs->r[2]);
        regs->r[2]--;
        regs->r[3]-=0x58;
    } else {
        debug_printf("Count Hook: Suspending process pidx %d\n", pidx);
    }
}

void ioctl_7e_hook(trampoline_t_state * regs){
    int slc_res = wait_for_dev("/dev/slc01",15000000);
    debug_printf("Waiting for slc returned %d\n", slc_res);
    int zero = 0;
    slc_res = MCP_MountWithSubdir("/dev/slc01","sys","/vol/system_slc", NULL);
    debug_printf("Mounting slc returned %d\n", slc_res);
}

int mcp_mlc_wait_hook(char* dev, int timeout){
    debug_printf("MLC Wait Hook: %s\n", dev);
    int res = MCP_IVS_stuff();
    debug_printf("MCP_IVS_stuff returned %d\n", res);
    res = resume_process(UMS_PROCESS_IDX);
    debug_printf("Manually resuming UMS returned %d\n", res);
    do {
        debug_printf("Waiting for %s\n", dev);
    } while(res = wait_for_dev(dev, 1500000));
    debug_printf("Device %s found\n", dev);
    return res;
}

int mcp_mlc_mount_hook(char* dev, char* bind_dir, char* mount_point, int owner, int (*org_mount)(char*, char*, char*, int)){
    int res = org_mount(dev, bind_dir, mount_point, owner);
    debug_printf("MCP_MountWithSubdir(%s, %s, %s, %d) -> %d\n", dev, bind_dir, mount_point, owner, res);
    return res;
}

void mcp_mlc_unmount_after_hook(trampoline_t_state *regs){
    debug_printf("MCP MLC Unmount returned %d\n", regs->r[0]);
    suspend_process(UMS_PROCESS_IDX);
    suspend_process(36);
}

void ums_mcp_open_hook(trampoline_state *regs){
    debug_printf("UMS: MCP_Open returend 0x%x (%d)\n", regs->r[0], regs->r[0]);
}

void ums_entry_hook(trampoline_state* regs){
    debug_printf("FSUMSServerEntry\n");
}

void ums_devtype_hook(trampoline_state *regs){
    static bool first = true;
    if(first){
        regs->r[3] = DEVTYPE_MLC;
        first = false;
    }
}


void tm_OpenDir_hook(trampoline_t_state* regs){
    debug_printf("TitleManager opening dir: %s\n", regs->r[1]);
}

void tm_scan_hook(trampoline_t_state* regs){
    debug_printf("TitleManager scanning: %s\n", regs->r[0]);
}

void nsec_read_file_hook(trampoline_state *regs){
    debug_printf("NSEC_OpenFile returned %d\n", regs->r[0]);
}

void nsec_cerstore_hook(trampoline_state *regs){
    debug_printf("NSEC_ReadFile returned %d\n", regs->r[0]);
}

static int hai_path_sprintf_hook_force_usb(char* parm1, char* parm2, char *fmt, char *dev, int (*sprintf)(char*, char*, char*, char*, char*), int lr, char *companion_file ){
    dev = "usb";
    return sprintf(parm1, parm2, fmt, dev, companion_file);
}

static void hai_read_devid_result_hook(trampoline_t_state *regs){
    debug_printf("hai_read_dev_id returned %d\n", regs->r[0]);
}

static void hai_read_devid_snprintf_hook(trampoline_t_state *regs){
    regs->r[2] = (int)"/dev/%s%02x";
    regs->r[3] = regs->r[4];
}

// This fn runs before everything else in kernel mode.
// It should be used to do extremely early patches
// (ie to BSP and kernel, which launches before MCP)
// It jumps to the real IOS kernel entry on exit.
__attribute__((target("arm")))
void kern_main()
{
    // Make sure relocs worked fine and mappings are good
    debug_printf("we in here plugin kern %p\n", kern_main);

    debug_printf("init_linking symbol at: %08x\n", wafel_find_symbol("init_linking"));

    trampoline_t_hook_before(0x05022a1a, pm_system_resume_hook);
    trampoline_t_hook_before(0x050229dc, pm_resume_count_hook);

    trampoline_t_hook_before(0x050229e2, pm_resume_offset_hook);

    trampoline_t_hook_before(0x05023084, pm_suspend_params_hook);
    trampoline_t_hook_before(0x050232a4, pm_suspend_count_hook);

    // hai should always use USB
    trampoline_t_blreplace(0x051001d6, hai_path_sprintf_hook_force_usb);

    // also set umsBlkDevID for /dev/mlc01
    ASM_T_PATCH_K(0x05100054, "nop\nnop");

    trampoline_t_hook_before(0x050083ac, hai_read_devid_result_hook);
    trampoline_t_hook_before(0x05100062, hai_read_devid_snprintf_hook);

    trampoline_t_blreplace(0x05027d18, mcp_mlc_wait_hook);
    trampoline_t_blreplace(0x05027d54, mcp_mlc_mount_hook);
    trampoline_t_hook_before(0x050283aa, mcp_mlc_unmount_after_hook);

    //trampoline_t_hook_before(0x05024ea4, ioctl_7e_hook);
    // NOP out org SLC mount
    //ASM_T_PATCH_K(0x05027cc8, "nop\nnop");

    // Force UHS to reinit, even on reload
    ASM_PATCH_K(0x10100c90, "nop");
    ASM_PATCH_K(0x10100cb8, "nop");

    trampoline_hook_before(0x1077dda4, ums_mcp_open_hook);
    trampoline_hook_before(0x1070077c, ums_entry_hook);
    // change type to MLC
    trampoline_hook_before(0x1077edac, ums_devtype_hook);

    // Patch out MCP dependency of UMS. IVS Stuff need to be called elsewhere
    ASM_PATCH_K(0x1077dda0, "mov r0, #0"); // handle = MCP_open()
    ASM_PATCH_K(0x1077ddac, "mov r0, #0"); // MCP_Ioctl0x7e(handle)
    ASM_PATCH_K(0x1077df44, "nop"); // MCP_Close(handle)
    ASM_PATCH_K(0x1077dfd4, "nop"); // MCP_Close(handle)
 
    // Use crypto handle for USB (0x12) for the mlc type (originally 0x11)
    ASM_PATCH_K(0x1074362c, "mov r3, #0x12");

    // Make the Wii U think it's the kiosk which attaches the eMMC as mlcorig
    ASM_PATCH_K(0x10700044, "mov r0,#1 \n bx lr");


    // trampoline_t_hook_before(0x050158dc, tm_OpenDir_hook);
    // trampoline_t_hook_before(0x05015850, tm_scan_hook);

    boot_info_t *boot_info = (boot_info_t*)0x050a443b;
    size_t boot_info_size;
    int res = prsh_get_entry("boot_info", (void**)&boot_info, &boot_info_size);
    if(res>=0 && boot_info_size>=12){
        shutdown_from_hai = boot_info->boot_state & 0x00100000;
        debug_printf("boot_state: %p\n", boot_info->boot_state);
    }
    else {
        debug_printf("No valid boot_info found: %d, %p, %p\n", res, boot_info, boot_info_size);
    }

    debug_printf("done\n");
}

// This fn runs before MCP's main thread, and can be used
// to perform late patches and spawn threads under MCP.
// It must return.
void mcp_main()
{
    // Make sure relocs worked fine and mappings are good
	debug_printf("we in here plugin MCP %p\n", mcp_main);

    debug_printf("done\n");
}