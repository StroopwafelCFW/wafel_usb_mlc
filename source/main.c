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

#define UMS_PROCESS_IDX 47

int (*MCP_IVS_stuff)(void) = (void*)(0x0501c238|1);
int (*MCP_MountWithSubdir)(char*, char*, char *, void*) = (void*) 0x05015c7c;
int (*wait_for_dev)(const char*, u32) = (void*)(0x05016cd8|1);

int (*IOS_ResumeAsync)(int, int, int, void*, void*) = (void*)0x05056a1c;
int resume_process(u32 i){
    int parm1=*(int*)0x050b7fc8;
    int parm2=*(int*)0x050b7fc4;
    void *PM_MsgQueue = *(void**)0x05070a70;
    u32 IOS_Process_Struct_ARRAY_050711b0 = 0x050711b0;
    u32 IOS_Process_Struct_base = IOS_Process_Struct_ARRAY_050711b0 +0x58*i;
    int resource_handle_id = *(int*)(IOS_Process_Struct_base + 8);
    void* async_notify_request = *(void**)(IOS_Process_Struct_base + 0xc);
    *(int*)(IOS_Process_Struct_base + 0x10) = 5;
    *(int*)(IOS_Process_Struct_base + 0x4) = 0xfffffffc;
    return iosIpcResume(resource_handle_id, parm1, parm2);
    //return IOS_ResumeAsync(resource_handle_id, parm1, parm2, PM_MsgQueue, async_notify_request);
}

void pm_system_resume_hook(trampoline_t_state *regs){
    debug_printf("Calling IOS_ResumeAsync(%u, %u, %u, %u, %u)\n", regs->r[0], regs->r[1], regs->r[2], regs->r[3], regs->stack[0]);
}

void pm_resume_count_hook(trampoline_t_state * regs){
    u32 pidx = regs->r[2];
    char * name = *(char**)(0x050711b0 + 0x58*pidx +0x50);
    debug_printf("Resumed Process %d: %s\n", pidx, name);
    //msleep(3000);

    regs->r[0] = regs->r[2];

    // if(regs->r[2] == 33){
    //     //int mcp_handle = iosOpen("/dev/mcp", 0);
    //     // debug_printf("mcp_handle: %d\n", mcp_handle);
    //     // if(mcp_handle>0){
    //     //     int io_res = iosIoctl(mcp_handle, 0x7e, NULL, 0, 0, 0);
    //     //     debug_printf("IOCTL 0x7e returned %d\n", io_res);
    //     // }
    //     msleep(1000);
    //     int res = resume_process(UMS_PROCESS_IDX);
    //     debug_printf("Manually resuming UMS returned %d\n", res);
    //     //msleep(10000);
    // }
    // skip UMS, since it was already started by MCP
    // if(regs->r[2] == UMS_PROCESS_IDX-1)
    //     regs->r[2]++;
}

void pm_resume_offset_hook(trampoline_t_state *regs){
    // resume 47 before 34 and then skip it later
    int just_resumed = regs->r[0];
    // switch (just_resumed)
    // {
    //     case 33:
    //         regs->r[3] = 0x58 * 46;
    //         regs->r[2] = 0x28 + 0x58 * 46;
    //         break;
    //     case 34:
    //         regs->r[3] = 0x58 * 33;
    //         regs->r[2] = 0x28 + 0x58 * 33;
    //         break;
    //     case 46:
    //         regs->r[3] -= 0x58;
    //         regs->r[2] -= 0x58;
    //         break;
    // }

    debug_printf("resumed %d, r3: %p, r2: %p\n", just_resumed, regs->r[3], regs->r[2]);
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

void ums_mcp_open_hook(trampoline_state *regs){
    debug_printf("UMS: MCP_Open returend 0x%x (%d)\n", regs->r[0], regs->r[0]);
}

void ums_entry_hook(trampoline_state* regs){
    debug_printf("FSUMSServerEntry\n");
}

#define PROCESS_SIZE 0x58

typedef struct proccess_struct {
    u8 buff[PROCESS_SIZE];
} PACKED proccess_struct;

void reorder_processes(void){
    proccess_struct buff;
    proccess_struct *process_array = (void*)0x050711b0;
    memcpy(&buff, process_array + 47, PROCESS_SIZE);
    // for(int i=47; i>33; i--){
    //     debug_printf("copy process struct from %p to %p\n", process_array+i-1, process_array+i);
    //     memcpy(process_array+i, process_array+i-1, PROCESS_SIZE);
    // }
    memcpy(process_array+33, &buff, PROCESS_SIZE);
}

void pm_resume_preloop_hook(trampoline_t_state *regs){
    //reorder_processes();
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

    trampoline_t_hook_before(0x050227cc, pm_resume_preloop_hook);

    // reboot instead of reload
    ASM_T_PATCH_K(0x0501f578, "add r2, #4");

    //ASM_T_PATCH_K(0x0502317c, "nop\nnop");

    // Disable Panic in Kernel
    //ASM_PATCH_K(0x08129ce0, "bx lr\n");

    trampoline_t_blreplace(0x05027d18, mcp_mlc_wait_hook);
    //U32_PATCH_K(0x05066b9c+4, *(u32*)"/usb");

    //U32_PATCH_K(0x108015b0, *(u32*)"mlc");

    trampoline_t_blreplace(0x05027d54, mcp_mlc_mount_hook);

    //trampoline_t_hook_before(0x05024ea4, ioctl_7e_hook);
    // NOP out org SLC mount
    //ASM_T_PATCH_K(0x05027cc8, "nop\nnop");


    trampoline_hook_before(0x1077dda4, ums_mcp_open_hook);
    trampoline_hook_before(0x1070077c, ums_entry_hook);
    // change type to MLC
    ASM_PATCH_K(0x1077eda0, "mov r3,#5");

    // ignore IVS error
    // ASM_PATCH_K(0x1077dda8, "nop");
    // ASM_PATCH_K(0x1077ddb4, "nop");

    // Patch out MCP dependency of UMS. IVS Stuff need to be called elsewhere
    ASM_PATCH_K(0x1077dda0, "mov r0, #0"); // handle = MCP_open()
    ASM_PATCH_K(0x1077ddac, "mov r0, #0"); // MCP_Ioctl0x7e(handle)
    ASM_PATCH_K(0x1077df44, "nop"); // MCP_Close(handle)
    ASM_PATCH_K(0x1077dfd4, "nop"); // MCP_Close(handle)
 
    // Use crypto handle for USB (0x12) for the mlc type (originally 0x11)
    ASM_PATCH_K(0x1074362c, "moveq r3, #0x12");


    trampoline_t_hook_before(0x050158dc, tm_OpenDir_hook);
    trampoline_t_hook_before(0x05015850, tm_scan_hook);


    // trampoline_hook_before(0xe100eb50, nsec_read_file_hook);
    // trampoline_hook_before(0xe100d7c8, nsec_cerstore_hook);

    //ASM_PATCH_K(0xe100d7c4, "mov r0, #0");

    // don't start PPC
    //ASM_T_PATCH_K(0x050340ee, "mov r0, #0\nnop")

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