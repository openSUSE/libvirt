/*
 * openvz_driver.c: core driver methods for managing OpenVZ VEs
 *
 * Copyright (C) 2006, 2007 Binary Karma
 * Copyright (C) 2006 Shuveb Hussain
 * Copyright (C) 2007 Anoop Joe Cyriac
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Authors:
 * Shuveb Hussain <shuveb@binarykarma.com>
 * Anoop Joe Cyriac <anoop@binarykarma.com>
 *
 */

#ifdef WITH_OPENVZ

#include <config.h>

#include <sys/types.h>
#include <sys/poll.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/wait.h>

#include "internal.h"
#include "openvz_driver.h"
#include "event.h"
#include "buf.h"
#include "util.h"
#include "openvz_conf.h"
#include "nodeinfo.h"
#include "memory.h"

#define OPENVZ_MAX_ARG 28
#define CMDBUF_LEN 1488
#define CMDOP_LEN 288

static virDomainPtr openvzDomainLookupByID(virConnectPtr conn, int id);
static char *openvzGetOSType(virDomainPtr dom);
static int openvzGetNodeInfo(virConnectPtr conn, virNodeInfoPtr nodeinfo);
static virDomainPtr openvzDomainLookupByUUID(virConnectPtr conn, const unsigned char *uuid);
static virDomainPtr openvzDomainLookupByName(virConnectPtr conn, const char *name);
static int openvzDomainGetInfo(virDomainPtr dom, virDomainInfoPtr info);
static int openvzDomainShutdown(virDomainPtr dom);
static int openvzDomainReboot(virDomainPtr dom, unsigned int flags);
static int openvzDomainCreate(virDomainPtr dom);
static virDrvOpenStatus openvzOpen(virConnectPtr conn,
                                 xmlURIPtr uri,
                                 virConnectAuthPtr auth ATTRIBUTE_UNUSED,
                                 int flags ATTRIBUTE_UNUSED);
static int openvzClose(virConnectPtr conn);
static const char *openvzGetType(virConnectPtr conn ATTRIBUTE_UNUSED);
static int openvzListDomains(virConnectPtr conn, int *ids, int nids);
static int openvzNumDomains(virConnectPtr conn);
static int openvzListDefinedDomains(virConnectPtr conn, char **const names, int nnames);
static int openvzNumDefinedDomains(virConnectPtr conn);
static int openvzStartup(void);
static int openvzShutdown(void);
static int openvzReload(void);
static int openvzActive(void);

static virDomainPtr openvzDomainDefineXML(virConnectPtr conn, const char *xml);
static virDomainPtr openvzDomainCreateLinux(virConnectPtr conn, const char *xml,
        unsigned int flags ATTRIBUTE_UNUSED);

static int openvzDomainUndefine(virDomainPtr dom);
static int convCmdbufExec(char cmdbuf[], char *cmdExec[]);
static void cmdExecFree(char *cmdExec[]);

struct openvz_driver ovz_driver;

static int convCmdbufExec(char cmdbuf[], char *cmdExec[])
{
    int i=0, limit = OPENVZ_MAX_ARG - 1;
    char cmdWord[CMDOP_LEN];
    while(*cmdbuf)
    {
        if(i >= limit)
        {
            cmdExec[i] = NULL;
            return -1;
        }
        sscanf(cmdbuf, "%s", cmdWord);
        cmdbuf += strlen(cmdWord);
        while(*cmdbuf == ' ') cmdbuf++;
        cmdExec[i++] = strdup(cmdWord);
    }
    cmdExec[i] = NULL;
    return i;
}

static void cmdExecFree(char *cmdExec[])
{
    int i=-1;
    while(cmdExec[++i])
    {
        VIR_FREE(cmdExec[i]);
    }
}

/* generate arguments to create OpenVZ container
   return -1 - error
           0 - OK
*/
static int openvzDomainDefineCmd(virConnectPtr conn,
                                 char *args[],
                                 int maxarg,
                                 struct openvz_vm_def *vmdef)
{
    int narg;

    for (narg = 0; narg < maxarg; narg++)
        args[narg] = NULL;

    if (vmdef == NULL){
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
                   _("Container is not defined"));
        return -1;
    }

#define ADD_ARG(thisarg)                                                \
    do {                                                                \
        if (narg >= maxarg)                                             \
                 goto no_memory;                                        \
        args[narg++] = thisarg;                                         \
    } while (0)

#define ADD_ARG_LIT(thisarg)                                            \
    do {                                                                \
        if (narg >= maxarg)                                             \
                 goto no_memory;                                        \
        if ((args[narg++] = strdup(thisarg)) == NULL)                   \
            goto no_memory;                                             \
    } while (0)

    narg = 0;
    ADD_ARG_LIT(VZCTL);
    ADD_ARG_LIT("--quiet");
    ADD_ARG_LIT("create");
    ADD_ARG_LIT(vmdef->name);

    if ((vmdef->fs.tmpl && *(vmdef->fs.tmpl))) {
        ADD_ARG_LIT("--ostemplate");
        ADD_ARG_LIT(vmdef->fs.tmpl);
    }
    if ((vmdef->profile && *(vmdef->profile))) {
        ADD_ARG_LIT("--config");
        ADD_ARG_LIT(vmdef->profile);
    }
    if ((vmdef->net.ips->ip && *(vmdef->net.ips->ip))) {
        ADD_ARG_LIT("--ipadd");
        ADD_ARG_LIT(vmdef->net.ips->ip);
    }
    if ((vmdef->net.hostname && *(vmdef->net.hostname))) {
        ADD_ARG_LIT("--hostname");
        ADD_ARG_LIT(vmdef->net.hostname);
    }

    ADD_ARG(NULL);
    return 0;
 no_memory:
    openvzError(conn, VIR_ERR_INTERNAL_ERROR,
                _("Could not put argument to %s"), VZCTL);
    return -1;
#undef ADD_ARG
#undef ADD_ARG_LIT
}


static virDomainPtr openvzDomainLookupByID(virConnectPtr conn,
                                   int id) {
    struct openvz_driver *driver = (struct openvz_driver *)conn->privateData;
    struct openvz_vm *vm;
    virDomainPtr dom;

    vm = openvzFindVMByID(driver, id);

    if (!vm) { /*try to find by name*/
        char name[OPENVZ_NAME_MAX];
        if (snprintf(name, OPENVZ_NAME_MAX, "%d",id) >= OPENVZ_NAME_MAX) {
            openvzError(conn, VIR_ERR_INTERNAL_ERROR, _("Too long domain name"));
            return NULL;
        }
        vm = openvzFindVMByName(driver, name);
    }

    if (!vm) {
        openvzError(conn, VIR_ERR_NO_DOMAIN, NULL);
        return NULL;
    }

    dom = virGetDomain(conn, vm->vmdef->name, vm->vmdef->uuid);
    if (!dom) {
        openvzError(conn, VIR_ERR_NO_MEMORY, _("virDomainPtr"));
        return NULL;
    }

    dom->id = vm->vpsid;
    return dom;
}

static char *openvzGetOSType(virDomainPtr dom ATTRIBUTE_UNUSED)
{
    /* OpenVZ runs on Linux and runs only Linux */
    return strdup("linux");
}


static virDomainPtr openvzDomainLookupByUUID(virConnectPtr conn,
                                     const unsigned char *uuid) {
    struct  openvz_driver *driver = (struct openvz_driver *)conn->privateData;
    struct openvz_vm *vm = openvzFindVMByUUID(driver, uuid);
    virDomainPtr dom;

    if (!vm) {
        openvzError(conn, VIR_ERR_NO_DOMAIN, NULL);
        return NULL;
    }

    dom = virGetDomain(conn, vm->vmdef->name, vm->vmdef->uuid);
    if (!dom) {
        openvzError(conn, VIR_ERR_NO_MEMORY, _("virDomainPtr"));
        return NULL;
    }

    dom->id = vm->vpsid;
    return dom;
}

static virDomainPtr openvzDomainLookupByName(virConnectPtr conn,
                                     const char *name) {
    struct openvz_driver *driver = (struct openvz_driver *)conn->privateData;
    struct openvz_vm *vm = openvzFindVMByName(driver, name);
    virDomainPtr dom;

    if (!vm) {
        openvzError(conn, VIR_ERR_NO_DOMAIN, NULL);
        return NULL;
    }

    dom = virGetDomain(conn, vm->vmdef->name, vm->vmdef->uuid);
    if (!dom) {
        openvzError(conn, VIR_ERR_NO_MEMORY, _("virDomainPtr"));
        return NULL;
    }

    dom->id = vm->vpsid;
    return dom;
}

static int openvzDomainGetInfo(virDomainPtr dom,
                       virDomainInfoPtr info) {
    struct openvz_driver *driver = (struct openvz_driver *)dom->conn->privateData;
    struct openvz_vm *vm = openvzFindVMByUUID(driver, dom->uuid);

    if (!vm) {
        openvzError(dom->conn, VIR_ERR_INVALID_DOMAIN,
              _("no domain with matching uuid"));
        return -1;
    }

    info->state = vm->status;

    /* TODO These need to be calculated differently for OpenVZ */
    //info->cpuTime =
    //info->maxMem = vm->def->maxmem;
    //info->memory = vm->def->memory;
    //info->nrVirtCpu = vm->def->vcpus;
    return 0;
}

static int openvzDomainShutdown(virDomainPtr dom) {
    struct openvz_driver *driver = (struct openvz_driver *)dom->conn->privateData;
    struct openvz_vm *vm = openvzFindVMByID(driver, dom->id);
    const char *prog[] = {VZCTL, "--quiet", "stop", vm->vmdef->name, NULL};

    if (!vm) {
        openvzError(dom->conn, VIR_ERR_INVALID_DOMAIN,
              _("no domain with matching id"));
        return -1;
    }

    if (vm->status != VIR_DOMAIN_RUNNING) {
        openvzError(dom->conn, VIR_ERR_OPERATION_DENIED,
              _("domain is not in running state"));
        return -1;
    }

    if (virRun(dom->conn, (char **)prog, NULL) < 0) {
        openvzError(dom->conn, VIR_ERR_INTERNAL_ERROR,
              _("Could not exec %s"), VZCTL);
        return -1;
    }

    vm->vpsid = -1;
    vm->status = VIR_DOMAIN_SHUTOFF;
    ovz_driver.num_inactive ++;
    ovz_driver.num_active --;

    return 0;
}

static int openvzDomainReboot(virDomainPtr dom,
                              unsigned int flags ATTRIBUTE_UNUSED) {
    struct openvz_driver *driver = (struct openvz_driver *)dom->conn->privateData;
    struct openvz_vm *vm = openvzFindVMByID(driver, dom->id);
    const char *prog[] = {VZCTL, "--quiet", "restart", vm->vmdef->name, NULL};

    if (!vm) {
        openvzError(dom->conn, VIR_ERR_INVALID_DOMAIN,
              _("no domain with matching id"));
        return -1;
    }

    if (vm->status != VIR_DOMAIN_RUNNING) {
        openvzError(dom->conn, VIR_ERR_OPERATION_DENIED,
              _("domain is not in running state"));
        return -1;
    }

    if (virRun(dom->conn, (char **)prog, NULL) < 0) {
        openvzError(dom->conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        return -1;
    }

    return 0;
}

static virDomainPtr
openvzDomainDefineXML(virConnectPtr conn, const char *xml)
{
    struct openvz_driver *driver = (struct openvz_driver *) conn->privateData;
    struct openvz_vm_def *vmdef = NULL;
    struct openvz_vm *vm = NULL;
    virDomainPtr dom = NULL;
    char *prog[OPENVZ_MAX_ARG];
    prog[0] = NULL;

    if ((vmdef = openvzParseVMDef(conn, xml, NULL)) == NULL)
        return NULL;

    vm = openvzFindVMByID(driver, strtoI(vmdef->name));
    if (vm) {
        openvzLog(OPENVZ_ERR, _("Already an OPENVZ VM active with the id '%s'"),
                  vmdef->name);
        return NULL;
    }
    if (!(vm = openvzAssignVMDef(conn, driver, vmdef))) {
        openvzFreeVMDef(vmdef);
        openvzLog(OPENVZ_ERR, "%s", _("Error creating OPENVZ VM"));
    }

    if (openvzDomainDefineCmd(conn, prog, OPENVZ_MAX_ARG, vmdef) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
                _("Error creating command for container"));
        goto exit;
    }

    if (virRun(conn, (char **)prog, NULL) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        goto exit;
    }

    dom = virGetDomain(conn, vm->vmdef->name, vm->vmdef->uuid);
    if (dom)
        dom->id = vm->vpsid;
 exit:
    cmdExecFree(prog);
    return dom;
}

static virDomainPtr
openvzDomainCreateLinux(virConnectPtr conn, const char *xml,
                        unsigned int flags ATTRIBUTE_UNUSED)
{
    struct openvz_vm_def *vmdef = NULL;
    struct openvz_vm *vm = NULL;
    virDomainPtr dom = NULL;
    struct openvz_driver *driver = (struct openvz_driver *) conn->privateData;
    const char *progstart[] = {VZCTL, "--quiet", "start", NULL, NULL};
    char *progcreate[OPENVZ_MAX_ARG];
    progcreate[0] = NULL;

    if (!(vmdef = openvzParseVMDef(conn, xml, NULL)))
        return NULL;

    vm = openvzFindVMByID(driver, strtoI(vmdef->name));
    if (vm) {
        openvzFreeVMDef(vmdef);
        openvzLog(OPENVZ_ERR,
                  _("Already an OPENVZ VM defined with the id '%d'"),
                strtoI(vmdef->name));
        return NULL;
    }
    if (!(vm = openvzAssignVMDef(conn, driver, vmdef))) {
        openvzLog(OPENVZ_ERR, "%s", _("Error creating OPENVZ VM"));
        return NULL;
    }

    if (openvzDomainDefineCmd(conn, progcreate, OPENVZ_MAX_ARG, vmdef) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
                _("Error creating command for container"));
        goto exit;
    }

    if (virRun(conn, (char **)progcreate, NULL) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        goto exit;
    }

    progstart[3] = vmdef->name;

    if (virRun(conn, (char **)progstart, NULL) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        goto exit;
    }

    sscanf(vmdef->name, "%d", &vm->vpsid);
    vm->status = VIR_DOMAIN_RUNNING;
    ovz_driver.num_inactive--;
    ovz_driver.num_active++;

    dom = virGetDomain(conn, vm->vmdef->name, vm->vmdef->uuid);
    if (dom)
        dom->id = vm->vpsid;
 exit:
    cmdExecFree(progcreate);
    return dom;
}

static int
openvzDomainCreate(virDomainPtr dom)
{
    struct openvz_driver *driver = (struct openvz_driver *)dom->conn->privateData;
    struct openvz_vm *vm = openvzFindVMByName(driver, dom->name);
    const char *prog[] = {VZCTL, "--quiet", "start", vm->vmdef->name, NULL };

    if (!vm) {
        openvzError(dom->conn, VIR_ERR_INVALID_DOMAIN,
              _("no domain with matching id"));
        return -1;
    }

    if (vm->status != VIR_DOMAIN_SHUTOFF) {
        openvzError(dom->conn, VIR_ERR_OPERATION_DENIED,
              _("domain is not in shutoff state"));
        return -1;
    }

    if (virRun(dom->conn, (char **)prog, NULL) < 0) {
        openvzError(dom->conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        return -1;
    }

    sscanf(vm->vmdef->name, "%d", &vm->vpsid);
    vm->status = VIR_DOMAIN_RUNNING;
    ovz_driver.num_inactive --;
    ovz_driver.num_active ++;

    return 0;
}

static int
openvzDomainUndefine(virDomainPtr dom)
{
    virConnectPtr conn= dom->conn;
    struct openvz_driver *driver = (struct openvz_driver *) conn->privateData;
    struct openvz_vm *vm = openvzFindVMByUUID(driver, dom->uuid);
    const char *prog[] = { VZCTL, "--quiet", "destroy", vm->vmdef->name, NULL };

    if (!vm) {
        openvzError(conn, VIR_ERR_INVALID_DOMAIN, _("no domain with matching uuid"));
        return -1;
    }

    if (openvzIsActiveVM(vm)) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR, _("cannot delete active domain"));
        return -1;
    }

    if (virRun(conn, (char **)prog, NULL) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZCTL);
        return -1;
    }

    openvzRemoveInactiveVM(driver, vm);
    return 0;
}

static int
openvzDomainSetAutostart(virDomainPtr dom, int autostart)
{
    virConnectPtr conn= dom->conn;
    struct openvz_driver *driver = (struct openvz_driver *) conn->privateData;
    struct openvz_vm *vm = openvzFindVMByUUID(driver, dom->uuid);
    const char *prog[] = { VZCTL, "--quiet", "set", vm->vmdef->name,
                           "--onboot", autostart ? "yes" : "no",
                           "--save", NULL };

    if (!vm) {
        openvzError(conn, VIR_ERR_INVALID_DOMAIN, _("no domain with matching uuid"));
        return -1;
    }

    if (virRun(conn, (char **)prog, NULL) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR, _("Could not exec %s"), VZCTL);
        return -1;
    }

    return 0;
}

static int
openvzDomainGetAutostart(virDomainPtr dom, int *autostart)
{
    virConnectPtr conn= dom->conn;
    struct openvz_driver *driver = (struct openvz_driver *) conn->privateData;
    struct openvz_vm *vm = openvzFindVMByUUID(driver, dom->uuid);
    char value[1024];

    if (!vm) {
        openvzError(conn, VIR_ERR_INVALID_DOMAIN, _("no domain with matching uuid"));
        return -1;
    }

    if (openvzReadConfigParam(vm->vpsid , "ONBOOT", value, sizeof(value)) < 0) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR, _("Cound not read container config"));
        return -1;
    }

    *autostart = 0;
    if (STREQ(value,"yes"))
        *autostart = 1;

    return 0;
}

static const char *openvzProbe(void)
{
#ifdef __linux__
    if ((getuid() == 0) && (virFileExists("/proc/vz")))
        return("openvz:///system");
#endif
    return(NULL);
}

static virDrvOpenStatus openvzOpen(virConnectPtr conn,
                                 xmlURIPtr uri,
                                 virConnectAuthPtr auth ATTRIBUTE_UNUSED,
                                 int flags ATTRIBUTE_UNUSED)
{
   struct openvz_vm *vms;

    /*Just check if the user is root. Nothing really to open for OpenVZ */
   if (getuid()) { // OpenVZ tools can only be used by r00t
           return VIR_DRV_OPEN_DECLINED;
   } else {
       if (uri == NULL || uri->scheme == NULL || uri->path == NULL)
                   return VIR_DRV_OPEN_DECLINED;
       if (STRNEQ (uri->scheme, "openvz"))
                   return VIR_DRV_OPEN_DECLINED;
       if (STRNEQ (uri->path, "/system"))
                   return VIR_DRV_OPEN_DECLINED;
   }
    /* See if we are running an OpenVZ enabled kernel */
   if(access("/proc/vz/veinfo", F_OK) == -1 ||
               access("/proc/user_beancounters", F_OK) == -1) {
       return VIR_DRV_OPEN_DECLINED;
   }

   conn->privateData = &ovz_driver;

   virStateInitialize();
   vms = openvzGetVPSInfo(conn);
   ovz_driver.vms = vms;

   return VIR_DRV_OPEN_SUCCESS;
};

static int openvzClose(virConnectPtr conn) {

    struct openvz_driver *driver = (struct openvz_driver *)conn->privateData;
    struct openvz_vm *vm = driver->vms;

    while(vm) {
        openvzFreeVMDef(vm->vmdef);
        vm = vm->next;
    }
    vm = driver->vms;
    while (vm) {
        struct openvz_vm *prev = vm;
        vm = vm->next;
        free(prev);
    }

    conn->privateData = NULL;

    return 0;
}

static const char *openvzGetType(virConnectPtr conn ATTRIBUTE_UNUSED) {
    return strdup("OpenVZ");
}

static int openvzGetNodeInfo(virConnectPtr conn,
                             virNodeInfoPtr nodeinfo) {
    return virNodeInfoPopulate(conn, nodeinfo);
}

static int openvzListDomains(virConnectPtr conn, int *ids, int nids) {
    int got = 0;
    int veid, pid, outfd, errfd;
    int ret;
    char buf[32];
    const char *cmd[] = {VZLIST, "-ovpsid", "-H" , NULL};

    ret = virExec(conn, (char **)cmd, &pid, -1, &outfd, &errfd);
    if(ret == -1) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZLIST);
        return -1;
    }

    while(got < nids){
        ret = openvz_readline(outfd, buf, 32);
        if(!ret) break;
        sscanf(buf, "%d", &veid);
        ids[got] = veid;
        got ++;
    }
    waitpid(pid, NULL, 0);

    return got;
}

static int openvzNumDomains(virConnectPtr conn ATTRIBUTE_UNUSED) {
    return ovz_driver.num_active;
}

static int openvzListDefinedDomains(virConnectPtr conn,
                            char **const names, int nnames) {
    int got = 0;
    int veid, pid, outfd, errfd, ret;
    char vpsname[OPENVZ_NAME_MAX];
    char buf[32];
    const char *cmd[] = {VZLIST, "-ovpsid", "-H", "-S", NULL};

    /* the -S options lists only stopped domains */
    ret = virExec(conn, (char **)cmd, &pid, -1, &outfd, &errfd);
    if(ret == -1) {
        openvzError(conn, VIR_ERR_INTERNAL_ERROR,
               _("Could not exec %s"), VZLIST);
        return -1;
    }

    while(got < nnames){
        ret = openvz_readline(outfd, buf, 32);
        if(!ret) break;
        sscanf(buf, "%d\n", &veid);
        sprintf(vpsname, "%d", veid);
        names[got] = strdup(vpsname);
        got ++;
    }
    waitpid(pid, NULL, 0);
    return got;
}

static int openvzNumDefinedDomains(virConnectPtr conn ATTRIBUTE_UNUSED) {
    return ovz_driver.num_inactive;
}

static int openvzStartup(void) {
    openvzAssignUUIDs();

    return 0;
}

static int openvzShutdown(void) {

    return 0;
}

static int openvzReload(void) {

    return 0;
}

static int openvzActive(void) {

    return 1;
}

static virDriver openvzDriver = {
    VIR_DRV_OPENVZ,
    "OPENVZ",
    LIBVIR_VERSION_NUMBER,
    openvzProbe, /* probe */
    openvzOpen, /* open */
    openvzClose, /* close */
    NULL, /* supports_feature */
    openvzGetType, /* type */
    NULL, /* version */
    NULL, /* hostname */
    NULL, /* uri */
    NULL, /* getMaxVcpus */
    openvzGetNodeInfo, /* nodeGetInfo */
    NULL, /* getCapabilities */
    openvzListDomains, /* listDomains */
    openvzNumDomains, /* numOfDomains */
    openvzDomainCreateLinux, /* domainCreateLinux */
    openvzDomainLookupByID, /* domainLookupByID */
    openvzDomainLookupByUUID, /* domainLookupByUUID */
    openvzDomainLookupByName, /* domainLookupByName */
    NULL, /* domainSuspend */
    NULL, /* domainResume */
    openvzDomainShutdown, /* domainShutdown */
    openvzDomainReboot, /* domainReboot */
    openvzDomainShutdown, /* domainDestroy */
    openvzGetOSType, /* domainGetOSType */
    NULL, /* domainGetMaxMemory */
    NULL, /* domainSetMaxMemory */
    NULL, /* domainSetMemory */
    openvzDomainGetInfo, /* domainGetInfo */
    NULL, /* domainSave */
    NULL, /* domainRestore */
    NULL, /* domainCoreDump */
    NULL, /* domainSetVcpus */
    NULL, /* domainPinVcpu */
    NULL, /* domainGetVcpus */
    NULL, /* domainGetMaxVcpus */
    NULL, /* domainDumpXML */
    openvzListDefinedDomains, /* listDomains */
    openvzNumDefinedDomains, /* numOfDomains */
    openvzDomainCreate, /* domainCreate */
    openvzDomainDefineXML, /* domainDefineXML */
    openvzDomainUndefine, /* domainUndefine */
    NULL, /* domainAttachDevice */
    NULL, /* domainDetachDevice */
    openvzDomainGetAutostart, /* domainGetAutostart */
    openvzDomainSetAutostart, /* domainSetAutostart */
    NULL, /* domainGetSchedulerType */
    NULL, /* domainGetSchedulerParameters */
    NULL, /* domainSetSchedulerParameters */
    NULL, /* domainMigratePrepare */
    NULL, /* domainMigratePerform */
    NULL, /* domainMigrateFinish */
    NULL, /* domainBlockStats */
    NULL, /* domainInterfaceStats */
    NULL, /* domainBlockPeek */
    NULL, /* domainMemoryPeek */
    NULL, /* nodeGetCellsFreeMemory */
    NULL, /* nodeGetFreeMemory */
};

static virStateDriver openvzStateDriver = {
    openvzStartup,
    openvzShutdown,
    openvzReload,
    openvzActive,
    NULL, /* sigHandler */
};

int openvzRegister(void) {
    virRegisterDriver(&openvzDriver);
    virRegisterStateDriver(&openvzStateDriver);
    return 0;
}

#endif /* WITH_OPENVZ */
