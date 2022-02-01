/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - ACPI(CA) System Interface (OSL)
 *  - Implementation for setting correct osl and osc settings. 
 */
#define __MODULE "ACPI"
#define __TRACE

#include <acpiinterface.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

/* OSC */
#define ACPI_OSC_QUERY_INDEX				0
#define ACPI_OSC_SUPPORT_INDEX				1
#define ACPI_OSC_CONTROL_INDEX				2

#define ACPI_OSC_QUERY_ENABLE				0x1

#define ACPI_OSC_SUPPORT_SB_PR3_SUPPORT		0x4
#define ACPI_OSC_SUPPORT_SB_APEI_SUPPORT	0x10

struct _acpi_osc {
	char *uuid;
	int revision;
	ACPI_BUFFER capabilities;
	ACPI_BUFFER retval;
};

#define OSI_STRING_LENGTH_MAX 64        /* arbitrary */
#define OSI_STRING_ENTRIES_MAX 16       /* arbitrary */

typedef struct OsiSetupEntry {
	char    String[OSI_STRING_LENGTH_MAX];
	uint8_t Enable;
} OsiSetupEntry_t;

static OsiSetupEntry_t g_osiFeatures[OSI_STRING_ENTRIES_MAX] = {
	{"Module Device", 1},
	{"Processor Device", 1},
	{"3.0 _SCP Extensions", 1},
	{"Processor Aggregator Device", 1},
    {"Linux-Dell-Video", 1},
    {"Linux-Lenovo-NV-HDMI-Audio", 1},
    {"Linux-HPI-Hybrid-Graphics", 1},
};

extern void AcpiUtConvertStringToUuid(char*, UINT8*);

/* Global ACPI Information */
char *sb_uuid_str = "0811B06E-4A27-44F9-8D60-3CBBC22E7B48";
char *osc_uuid_str = "33DB4D5B-1FF7-401C-9657-7441C03DD766";
char *osc_batt_uuid_str = "F18FC78B-0F15-4978-B793-53F833A1D35B";

/* Interface Handlers */
UINT32 AcpiOsi(ACPI_STRING InterfaceName, UINT32 Supported)
{
	if (!strcmp("Darwin", InterfaceName))
	{
		/* Apple firmware will behave poorly if it receives positive
		 * answers to "Darwin" and any other OS. Respond positively
		 * to Darwin and then disable all other vendor strings. */
		AcpiUpdateInterfaces(ACPI_DISABLE_ALL_VENDOR_STRINGS);
		Supported = ACPI_UINT32_MAX;
	}

	return Supported;
}

/* Run OSC Query */
ACPI_STATUS AcpiRunOscRequest(ACPI_HANDLE device, struct _acpi_osc *osc)
{
	ACPI_STATUS status = AE_ERROR;
	ACPI_OBJECT_LIST input;
	ACPI_OBJECT in_params[4];
	ACPI_OBJECT *out_param;
	uint8_t uuid[16];
	ACPI_BUFFER output;
	uint32_t query_status = 0;

	/* Sanity */
	if(osc == NULL)
		return AE_BAD_DATA;

	/* Convert */
	AcpiUtConvertStringToUuid(osc->uuid, uuid);

	/* Setup return object */
	osc->retval.Length = ACPI_ALLOCATE_BUFFER;
	osc->retval.Pointer = NULL;

	/* Setup output */
	output.Length = ACPI_ALLOCATE_BUFFER;
	output.Pointer = NULL;

	/* Set up list */
	input.Count = 4;
	input.Pointer = in_params;

	/* Set up parameters */
	in_params[0].Type = ACPI_TYPE_BUFFER;
	in_params[0].Buffer.Length = sizeof(uuid);
	in_params[0].Buffer.Pointer = uuid;

	in_params[1].Type = ACPI_TYPE_INTEGER;
	in_params[1].Integer.Value = osc->revision;

	in_params[2].Type = ACPI_TYPE_INTEGER;
	in_params[2].Integer.Value = (osc->capabilities.Length / sizeof(ACPI_SIZE));

	in_params[3].Type = ACPI_TYPE_BUFFER;
	in_params[3].Buffer.Length = osc->capabilities.Length;
	in_params[3].Buffer.Pointer = osc->capabilities.Pointer;

	/* Evaluate Object */
	status = AcpiEvaluateObject(device, "_OSC", &input, &output);

	/* Sanity */
	if (ACPI_FAILURE(status))
		return status;

	/* More Sanity */
	if (!output.Length)
		return AE_NULL_OBJECT;

	out_param = output.Pointer;

	/* Sanity */
	if (out_param->Type != ACPI_TYPE_BUFFER
		|| out_param->Buffer.Length != osc->capabilities.Length)
	{
		/* OSC Returned wrong type, sounds wierd */
		status = AE_TYPE;
		goto fail;
	}

	/* Now check error codes in query dword (Ignore bit 0) */
	query_status = *((uint32_t*)out_param->Buffer.Pointer);
	query_status &= ~(ACPI_OSC_QUERY_ENABLE);

	if (query_status)
	{
		TRACE("OSC Query Failed, Status Word: 0x%" PRIxIN "\n", query_status);
		status = AE_ERROR;
		goto fail;
	}

	/* Set return object */
	osc->retval.Length = out_param->Buffer.Length;
	osc->retval.Pointer = out_param->Buffer.Pointer;

	/* Allocate a new buffer */
	osc->retval.Pointer = kmalloc(out_param->Buffer.Length);

	/* Sanity */
	if (osc->retval.Pointer == NULL)
	{
		status = AE_NO_MEMORY;
		goto fail;
	}
	else
		memcpy(osc->retval.Pointer, out_param->Buffer.Pointer, out_param->Buffer.Length);

	/* Set ok */
	status = AE_OK;

fail:
	/* Free */
	kfree(output.Pointer);

	/* Return */
	if (status != AE_OK)
		osc->retval.Pointer = NULL;
	
	return status;
}

/* Run OSC Support */
void AcpiInitializeOsc(void)
{
	/* Decls */
	ACPI_HANDLE handle;
	uint32_t capabilities[2];
	struct _acpi_osc osc;
	uint32_t apei_support;

	/* Setup OSC */
	osc.uuid = sb_uuid_str;
	osc.revision = 1;
	osc.capabilities.Length = sizeof(capabilities);
	osc.capabilities.Pointer = capabilities;

	/* Setup capabilities */
	capabilities[ACPI_OSC_QUERY_INDEX] = ACPI_OSC_QUERY_ENABLE;
	capabilities[ACPI_OSC_SUPPORT_INDEX] = ACPI_OSC_SUPPORT_SB_PR3_SUPPORT;

	/* Haha we have like zero power management support, gg wp */

	/* Get root handle */
	if (ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB", &handle)))
		return;

	/* Run OSC Query */
	if (ACPI_SUCCESS(AcpiRunOscRequest(handle, &osc)))
	{
		/* Get capabilities */
		uint32_t *capabilitybuffer = osc.retval.Pointer;

		/* Did we get more than we asked for? */
		if (osc.retval.Length > ACPI_OSC_SUPPORT_INDEX)
			apei_support = capabilitybuffer[ACPI_OSC_SUPPORT_INDEX] & ACPI_OSC_SUPPORT_SB_APEI_SUPPORT;

		/* Cleanup */
		kfree(osc.retval.Pointer);
	}
}

/* Enable or Disable OSI Interface */
void AcpiOsiSetup(const char *OsiString)
{
	OsiSetupEntry_t *osi;
	uint8_t Enable = 1;
	int i;

	if (*OsiString == '!')
	{
		/* Go to next char */
		OsiString++;
		
		/* Disable all interfaces */
		if (*OsiString == '*')
		{
			/* Update*/
			AcpiUpdateInterfaces(ACPI_DISABLE_ALL_STRINGS);
			
			for (i = 0; i < OSI_STRING_ENTRIES_MAX; i++) 
			{
				osi = &g_osiFeatures[i];
				osi->Enable = 0;
			}
			
			return;
		}

		Enable = 0;
	}
	
	for (i = 0; i < OSI_STRING_ENTRIES_MAX; i++) 
	{
		osi = &g_osiFeatures[i];
		if (!strcmp(osi->String, OsiString))
		{
			osi->Enable = Enable;
			break;
		}
		else if (osi->String[0] == '\0')
		{
			osi->Enable = Enable;
			strncpy(osi->String, OsiString, OSI_STRING_LENGTH_MAX);
			break;
		}
	}
}

/* Install OSI Interfaces */
void AcpiOsiInstall(void)
{
	/* Variables */
	ACPI_STATUS Status;
	int i;
	OsiSetupEntry_t *osi;
	char *str;

	/* Install ALL OSI Interfaces */
	for (i = 0; i < OSI_STRING_ENTRIES_MAX; i++)
	{
		osi = &g_osiFeatures[i];
		str = osi->String;

		if (*str == '\0')
			break;

		if (osi->Enable)
			Status = AcpiInstallInterface(str);
		else {
			Status = AcpiRemoveInterface(str);
		}
	}
}
