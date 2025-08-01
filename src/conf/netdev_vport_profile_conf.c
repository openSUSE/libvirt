/*
 * Copyright (C) 2009-2014 Red Hat, Inc.
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "netdev_vport_profile_conf.h"
#include "virerror.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE


virNetDevVPortProfile *
virNetDevVPortProfileParse(xmlNodePtr node, unsigned int flags)
{
    g_autofree virNetDevVPortProfile *virtPort = NULL;
    xmlNodePtr parameters;

    virtPort = g_new0(virNetDevVPortProfile, 1);

    if (virXMLPropEnum(node, "type",
                       virNetDevVPortTypeFromString,
                       VIR_XML_PROP_NONZERO,
                       &virtPort->virtPortType) < 0) {
        return NULL;
    }

    if ((virtPort->virtPortType == VIR_NETDEV_VPORT_PROFILE_NONE) &&
        (flags & VIR_VPORT_XML_REQUIRE_TYPE)) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing required virtualport type"));
        return NULL;
    }

    if ((parameters = virXMLNodeGetSubelement(node, "parameters"))) {
        int rc;
        unsigned int val;
        g_autofree char *virtPortProfileID = NULL;

        virtPortProfileID =  virXMLPropString(parameters, "profileid");

        if ((rc = virXMLPropUInt(parameters, "managerid", 10,
                                 VIR_XML_PROP_NONE, &val)) < 0) {
            return NULL;
        }

        if (rc > 0) {
            if (val > 0xff) {
                virReportError(VIR_ERR_XML_ERROR, "%s",
                               _("value of managerid out of range"));
                return NULL;
            }

            virtPort->managerID = val;
            virtPort->managerID_specified = true;
        }

        if ((rc = virXMLPropUInt(parameters, "typeid", 10,
                                 VIR_XML_PROP_NONE, &val)) < 0) {
            return NULL;
        }

        if (rc > 0) {
            if (val > 0xffffff) {
                virReportError(VIR_ERR_XML_ERROR, "%s",
                               _("value for typeid out of range"));
                return NULL;
            }

            virtPort->typeID = val;
            virtPort->typeID_specified = true;
        }

        if ((rc = virXMLPropUInt(parameters, "typeidversion", 10,
                                 VIR_XML_PROP_NONE, &val)) < 0) {
            return NULL;
        }

        if (rc > 0) {
            if (val > 0xff) {
                virReportError(VIR_ERR_XML_ERROR, "%s",
                               _("value of typeidversion out of range"));
                return NULL;
            }

            virtPort->typeIDVersion = val;
            virtPort->typeIDVersion_specified = true;
        }

        if ((rc = virXMLPropUUID(parameters, "instanceid",
                                 VIR_XML_PROP_NONE, virtPort->instanceID)) < 0) {
            return NULL;
        }

        if (rc > 0)
            virtPort->instanceID_specified = true;

        if ((rc = virXMLPropUUID(parameters, "interfaceid",
                                 VIR_XML_PROP_NONE, virtPort->interfaceID)) < 0) {
            return NULL;
        }

        if (rc > 0)
            virtPort->interfaceID_specified = true;

        if (virtPortProfileID &&
            virStrcpyStatic(virtPort->profileID, virtPortProfileID) < 0) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("profileid parameter too long"));
            return NULL;
        }
    }

    /* generate default instanceID/interfaceID if appropriate */
    if (flags & VIR_VPORT_XML_GENERATE_MISSING_DEFAULTS) {
        if (!virtPort->instanceID_specified &&
            (virtPort->virtPortType == VIR_NETDEV_VPORT_PROFILE_8021QBG ||
             virtPort->virtPortType == VIR_NETDEV_VPORT_PROFILE_NONE)) {
            if (virUUIDGenerate(virtPort->instanceID) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("cannot generate a random uuid for instanceid"));
                return NULL;
            }
            virtPort->instanceID_specified = true;
        }
        if (!virtPort->interfaceID_specified &&
            (virtPort->virtPortType == VIR_NETDEV_VPORT_PROFILE_OPENVSWITCH ||
             virtPort->virtPortType == VIR_NETDEV_VPORT_PROFILE_NONE)) {
            if (virUUIDGenerate(virtPort->interfaceID) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("cannot generate a random uuid for interfaceid"));
                return NULL;
            }
            virtPort->interfaceID_specified = true;
        }
    }

    /* check for required/unsupported attributes */

    if ((flags & VIR_VPORT_XML_REQUIRE_ALL_ATTRIBUTES) &&
        (virNetDevVPortProfileCheckComplete(virtPort, false) < 0)) {
        return NULL;
    }

    if (virNetDevVPortProfileCheckNoExtras(virtPort) < 0)
        return NULL;

    return g_steal_pointer(&virtPort);
}


void
virNetDevVPortProfileFormat(const virNetDevVPortProfile *virtPort,
                            virBuffer *buf)
{
    virNetDevVPortProfileType type;
    bool noParameters;

    if (!virtPort)
        return;

    noParameters = !(virtPort->managerID_specified ||
                     virtPort->typeID_specified ||
                     virtPort->typeIDVersion_specified ||
                     virtPort->instanceID_specified ||
                     virtPort->profileID[0] ||
                     virtPort->interfaceID_specified);

    type = virtPort->virtPortType;
    if (type == VIR_NETDEV_VPORT_PROFILE_NONE) {
        if (noParameters)
            return;
        virBufferAddLit(buf, "<virtualport>\n");
    } else {
        if (noParameters) {
            virBufferAsprintf(buf, "<virtualport type='%s'/>\n",
                              virNetDevVPortTypeToString(type));
            return;
        } else {
            virBufferAsprintf(buf, "<virtualport type='%s'>\n",
                              virNetDevVPortTypeToString(type));
        }
    }
    virBufferAdjustIndent(buf, 2);
    virBufferAddLit(buf, "<parameters");

    if (virtPort->managerID_specified &&
        (type == VIR_NETDEV_VPORT_PROFILE_8021QBG ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        virBufferAsprintf(buf, " managerid='%d'", virtPort->managerID);
    }
    if (virtPort->typeID_specified &&
        (type == VIR_NETDEV_VPORT_PROFILE_8021QBG ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        virBufferAsprintf(buf, " typeid='%d'", virtPort->typeID);
    }
    if (virtPort->typeIDVersion_specified &&
        (type == VIR_NETDEV_VPORT_PROFILE_8021QBG ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        virBufferAsprintf(buf, " typeidversion='%d'",
                          virtPort->typeIDVersion);
    }
    if (virtPort->instanceID_specified &&
        (type == VIR_NETDEV_VPORT_PROFILE_8021QBG ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];

        virUUIDFormat(virtPort->instanceID, uuidstr);
        virBufferAsprintf(buf, " instanceid='%s'", uuidstr);
    }
    if (virtPort->interfaceID_specified &&
        (type == VIR_NETDEV_VPORT_PROFILE_MIDONET ||
         type == VIR_NETDEV_VPORT_PROFILE_OPENVSWITCH ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];

        virUUIDFormat(virtPort->interfaceID, uuidstr);
        virBufferAsprintf(buf, " interfaceid='%s'", uuidstr);
    }
    if (virtPort->profileID[0] &&
        (type == VIR_NETDEV_VPORT_PROFILE_OPENVSWITCH ||
         type == VIR_NETDEV_VPORT_PROFILE_8021QBH ||
         type == VIR_NETDEV_VPORT_PROFILE_NONE)) {
        virBufferAsprintf(buf, " profileid='%s'", virtPort->profileID);
    }

    virBufferAddLit(buf, "/>\n");
    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</virtualport>\n");
    return;
}
