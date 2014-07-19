/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS X86-32 USB Core HID Driver
*/

/* Includes */
#include <arch.h>
#include <drivers\usb\usb.h>
#include <drivers\usb\hid\hid_manager.h>
#include <semaphore.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

/* Prototypes */
void usb_hid_parse_report_descriptor(usb_hid_driver_t *driver, uint8_t *report, uint32_t report_length);
void usb_hid_callback(void *driver, size_t bytes);

/* */

/* Collection List Helpers */
void usb_hid_collection_insert_child(usb_report_collection_t *collection, 
	usb_report_global_stats_t *stats, uint32_t hid_type, uint32_t type, void *data)
{
	usb_report_collection_item_t *curr_child;
	usb_report_collection_item_t *child =
		(usb_report_collection_item_t*)kmalloc(sizeof(usb_report_collection_item_t));
	child->type = type;
	child->input_type = hid_type;
	child->data = data;
	memcpy((void*)(&child->stats), stats, sizeof(usb_report_global_stats_t));
	child->link = NULL;

	/* Insert */
	if (collection->childs == NULL)
		collection->childs = child;
	else
	{
		curr_child = collection->childs;

		while (curr_child->link)
			curr_child = curr_child->link;

		curr_child->link = child;
	}
}

/* Extracts Bits from buffer */
uint64_t usb_get_bits(uint8_t *buffer, uint32_t bit_offset, uint32_t bit_length)
{
	uint64_t value = 0;
	uint32_t i = 0;
	uint32_t offset = bit_offset;

	while (i < bit_length)
	{
		uint8_t bits = ((offset % 8) + bit_length - i) < 8 ? bit_length % 8 : 8 - (offset % 8);
		
		/* */
		value |= ((buffer[offset / 8] >> (offset % 8)) & ((1 << bits) - 1)) << i;
		
		/* Skip to next set of bits */
		i += bits;
		
		/* Increase offset */
		offset += bits;
	}

	/* Done */
	return value;
}

/* Initialise Driver for a HID */
void usb_hid_initialise(usb_hc_device_t *device, uint32_t iface)
{
	uint8_t *buf_ptr = (uint8_t*)device->descriptors;
	uint32_t bytes_left = device->descriptors_length;
	usb_hid_descriptor_t *hid_descriptor = NULL;
	usb_hc_endpoint_t *hid_endpoint = NULL;
	usb_hid_driver_t *driver_data = NULL;
	usb_hc_t *hc = device->hcd;
	uint8_t *report_descriptor = NULL;
	uint32_t i, ep_itr;

	/* Locate the HID descriptor */
	while (bytes_left > 0)
	{
		/* Cast */
		uint8_t length = *buf_ptr;
		uint8_t type = *(buf_ptr + 1);

		/* Is this a HID descriptor ? */
		if (type == X86_USB_DESC_TYPE_HID)
		{
			hid_descriptor = (usb_hid_descriptor_t*)buf_ptr;
			printf("Report Descriptor Type: 0x%x, Length: 0x%x\n", 
				hid_descriptor->class_descriptor_type, hid_descriptor->class_descriptor_length);
			break;
		}
		else
		{
			buf_ptr += length;
			bytes_left -= length;
		}
	}

	/* Sanity */
	if (hid_descriptor == NULL)
	{
		printf("HID Descriptor did not exist.\n");
		return;
	}

	/* Locate interrupt IN endpoint 
	 * We only check endpoints related to this
	 * Interface */
	for (i = 0, ep_itr = 1; i < device->num_interfaces; i++)
	{
		if (i == iface)
		{
			uint32_t ep_max = ep_itr + device->interfaces[i]->endpoints;
			
			/* Ok, check endpoints */
			for (; ep_itr < ep_max; ep_itr++)
			{
				if (device->endpoints[ep_itr]->type == X86_USB_EP_TYPE_INTERRUPT
					&& device->endpoints[ep_itr]->direction == X86_USB_EP_DIRECTION_IN)
				{
					hid_endpoint = device->endpoints[ep_itr];
					break;
				}
			}

			/* Break */
			break;
		}
		else
			ep_itr += device->interfaces[i]->endpoints;
	}

	/* Sanity */
	if (hid_endpoint == NULL)
	{
		printf("HID Endpoint (In, Interrupt) did not exist.\n");
		return;
	}


	/* Get Report Descriptor */
	report_descriptor = (uint8_t*)kmalloc(hid_descriptor->class_descriptor_length);
	usb_function_get_descriptor(device->hcd, device->port, 
		report_descriptor, X86_USB_REQ_DIRECTION_IN | X86_USB_REQ_TARGET_INTERFACE, 
		hid_descriptor->class_descriptor_type,
		0, 0, hid_descriptor->class_descriptor_length);	/* 0x81 means IN, Interface */

	/* Switch to Report Protocol (ONLY if we are in boot protocol) */

	/* Allocate driver data */
	driver_data = (usb_hid_driver_t*)kmalloc(sizeof(usb_hid_driver_t));
	driver_data->previous_data_buffer = (uint8_t*)kmalloc(hid_endpoint->max_packet_size);
	driver_data->data_buffer = (uint8_t*)kmalloc(hid_endpoint->max_packet_size);
	driver_data->endpoint = hid_endpoint;
	driver_data->collection = NULL;

	/* Memset Databuffers */
	memset(driver_data->previous_data_buffer, 0, hid_endpoint->max_packet_size);
	memset(driver_data->data_buffer, 0, hid_endpoint->max_packet_size);

	/* Parse Report Descriptor */
	usb_hid_parse_report_descriptor(driver_data, report_descriptor, hid_descriptor->class_descriptor_length);

	/* Set idle :) */
	usb_function_send_packet(device->hcd, device->port, 0,
		X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE,
		X86_USB_REQ_SET_IDLE, 0, 0, 0, 0);

	/* Install Interrupt */
	hc->install_interrupt(hc->hc, device, hid_endpoint, driver_data->data_buffer, 
		hid_endpoint->max_packet_size, usb_hid_callback, driver_data);
}

/* Parses the report descriptor and stores it as 
 * collection tree */
void usb_hid_parse_report_descriptor(usb_hid_driver_t *driver, uint8_t *report, uint32_t report_length)
{
	uint32_t i = 0, j = 0, depth = 0;
	usb_report_collection_t *collection = NULL, *root_collection = NULL;
	usb_report_global_stats_t global_stats = { 0 };
	usb_report_item_stats_t item_stats = { 0 };
	uint32_t bit_offset = 0;
	uint32_t current_type = X86_USB_HID_TYPE_UNKNOWN;

	/* Null Report Id */
	global_stats.report_id = 0xFFFFFFFF;

	/* Loop through report */
	for (i = 0; i < report_length; /* Increase Manually */)
	{
		/* Bits 0-1 (Must be either 0, 1, 2 or 4) 3 = 4*/
		uint8_t size = report[i] & 0x03;

		/* Bits 2-3 */
		uint8_t type = report[i] & 0x0C;

		/* Bits 4-7 */
		uint8_t tag = report[i] & 0xF0;

		/* The packet */
		uint32_t packet = 0;

		/* Size Fixup */
		if (size == 3)
			size++;

		/* Get actual packet (The byte(s) after the header) */
		if (size == 1)
			packet = report[i + 1];
		else if (size == 2)
			packet = report[i + 1] | (uint32_t)((report[i + 2] << 8) & 0xFF00);
		else if (size == 4)
			packet = report[i + 1] | (uint32_t)((report[i + 2] << 8) & 0xFF00) 
			| (uint32_t)((report[i + 3] << 16) & 0xFF0000) | (uint32_t)((report[i + 4] << 24) & 0xFF000000);

		/* Update Report Pointer */
		i += (size + 1);

		/* Sanity, the first item outside an collection should be an COLLECTION!! */
		if (collection == NULL && type == X86_USB_REPORT_TYPE_MAIN && tag != X86_USB_REPORT_MAIN_TAG_COLLECTION)
			continue;

		/* Ok, we have 3 item groups. Main, Local & Global */
		switch (type)
		{
			/* Main Item */
			case X86_USB_REPORT_TYPE_MAIN:
			{
				/* 5 Main Item Groups: Input, Output, Feature, Collection, EndCollection */
				switch (tag)
				{
					/* Collection Item */
					case X86_USB_REPORT_MAIN_TAG_COLLECTION:
					{
						/* Create a new collection and set parent */
						usb_report_collection_t *next_collection = (usb_report_collection_t*)kmalloc(sizeof(usb_report_collection_t));
						next_collection->childs = NULL;
						next_collection->parent = NULL;

						if (collection == NULL)
						{
							collection = next_collection;
						}
						else
						{
							/* Set parent */
							next_collection->parent = collection;

							/* Create a child node */
							usb_hid_collection_insert_child(collection, &global_stats, current_type, 
								X86_USB_COLLECTION_TYPE_COLLECTION, next_collection);

							/* Enter collection */
							collection = next_collection;
						}

						/* Increase Depth */
						depth++;

					} break;

					/* End of Collection Item */
					case X86_USB_REPORT_MAIN_TAG_ENDCOLLECTION:
					{
						/* Move up */
						if (collection != NULL)
						{
							/* If parent is null, root collection is done (save it) */
							if (collection->parent != NULL)
								root_collection = collection;
								
							collection = collection->parent;
						}

						/* Decrease Depth */
						depth--;

					} break;

					/* Input Item */
					case X86_USB_REPORT_MAIN_TAG_INPUT:
					{
						usb_report_input_t *input_item =
							(usb_report_input_t*)kmalloc(sizeof(usb_report_input_t));

						/* What kind of input data? */
						if (packet & 0x1)
							input_item->flags = X86_USB_REPORT_INPUT_TYPE_CONSTANT;
						else
						{
							/* This is data, not constant */
							if (packet & 0x2)
							{
								/* Variable Input */
								if (packet & 0x4)
									input_item->flags = X86_USB_REPORT_INPUT_TYPE_RELATIVE; /* Data, Variable, Relative */
								else
									input_item->flags = X86_USB_REPORT_INPUT_TYPE_ABSOLUTE; /* Data, Variable, Absolute */
							}
							else
								input_item->flags = X86_USB_REPORT_INPUT_TYPE_ARRAY; /* Data, Array */
						}

						/* Set local stats */
						for (j = 0; j < 16; j++)
							input_item->stats.usages[j] = item_stats.usages[j];

						input_item->stats.usage_min = item_stats.usage_min;
						input_item->stats.usage_max = item_stats.usage_max;
						input_item->stats.bit_offset = bit_offset;

						/* Add to list */
						usb_hid_collection_insert_child(collection, &global_stats, current_type,
							X86_USB_COLLECTION_TYPE_INPUT, input_item);

						/* Increase Bitoffset Counter */
						bit_offset += global_stats.report_count * global_stats.report_size;
						
					} break;

					/* Output Item */
					case X86_USB_REPORT_MAIN_TAG_OUTPUT:
					{
						printf("%u: Output Item (%u)\n", depth, packet);
					} break;

					/* Feature Item */
					case X86_USB_REPORT_MAIN_TAG_FEATURE:
					{
						printf("%u: Feature Item (%u)\n", depth, packet);
					} break;
				}

				/* Reset Local Stats */
				for (j = 0; j < 16; j++)
					item_stats.usages[j] = 0;
				item_stats.usage_min = 0;
				item_stats.usage_max = 0;
				item_stats.bit_offset = 0;

			} break;

			/* Global Item */
			case X86_USB_REPORT_TYPE_GLOBAL:
			{
				switch (tag)
				{
					/* Usage Page */
					case X86_USB_REPORT_GLOBAL_TAG_USAGE_PAGE:
					{
						global_stats.usage = packet;
					} break;

					/* Logical Min & Max */
					case X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MIN:
					{
						/* New pair of log */
						if (global_stats.has_log_min != 0)
							global_stats.has_log_max = 0;
						
						global_stats.log_min = (int32_t)packet;
						global_stats.has_log_min = 1;

						if (global_stats.has_log_max != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(global_stats.log_min) >= (int)(global_stats.log_max))
							{
								/* Sign it */
								global_stats.log_min = ~(global_stats.log_min);
								global_stats.log_min++;
							}
						}

					} break;
					case X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MAX:
					{
						/* New pair of log */
						if (global_stats.has_log_max != 0)
							global_stats.has_log_min = 0;

						global_stats.log_max = (int32_t)packet;
						global_stats.has_log_max = 1;

						if (global_stats.has_log_min != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(global_stats.log_min) >= (int)(global_stats.log_max))
							{
								/* Sign it */
								global_stats.log_min = ~(global_stats.log_min);
								global_stats.log_min++;
							}
						}

					} break;

					/* Physical Min & Max */
					case X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MIN:
					{
						/* New pair of physical */
						if (global_stats.has_phys_min != 0)
							global_stats.has_phys_max = 0;

						global_stats.physical_min = (int32_t)packet;
						global_stats.has_phys_min = 1;

						if (global_stats.has_phys_max != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(global_stats.physical_min) >= (int)(global_stats.physical_max))
							{
								/* Sign it */
								global_stats.physical_min = ~(global_stats.physical_min);
								global_stats.physical_min++;
							}
						}

					} break;
					case X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MAX:
					{
						/* New pair of physical */
						if (global_stats.has_phys_max != 0)
							global_stats.has_phys_min = 0;

						global_stats.physical_max = (int32_t)packet;
						global_stats.has_phys_max = 1;

						if (global_stats.has_phys_min != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(global_stats.physical_min) >= (int)(global_stats.physical_max))
							{
								/* Sign it */
								global_stats.physical_min = ~(global_stats.physical_min);
								global_stats.physical_min++;
							}
						}

						/* Unit & Unit Exponent */
						case X86_USB_REPORT_GLOBAL_TAG_UNIT_VALUE:
						{
							global_stats.unit_type = (int32_t)packet;
						} break;
						case X86_USB_REPORT_GLOBAL_TAG_UNIT_EXPONENT:
						{
							global_stats.unit_exponent = (int32_t)packet;
						} break;

					} break;

					/* Report Items */
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_ID:
					{
						global_stats.report_id = packet;
					} break;
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_COUNT:
					{
						global_stats.report_count = packet;
					} break;
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_SIZE:
					{
						global_stats.report_size = packet;
					} break;
						
					/* Unhandled */
					default:
					{
						printf("%u: Global Item %u\n", depth, tag);
					} break;
				}

			} break;

			/* Local Item */
			case X86_USB_REPORT_TYPE_LOCAL:
			{
				switch (tag)
				{
					/* Usage */
					case X86_USB_REPORT_GLOBAL_TAG_USAGE_PAGE:
					{
						if (packet == X86_USB_REPORT_USAGE_POINTER
							|| packet == X86_USB_REPORT_USAGE_MOUSE)
							current_type = X86_USB_HID_TYPE_MOUSE;
						else if (packet == X86_USB_REPORT_USAGE_KEYBOARD)
							current_type = X86_USB_HID_TYPE_KEYBOARD;
						else if (packet == X86_USB_REPORT_USAGE_KEYPAD)
							current_type = X86_USB_HID_TYPE_KEYPAD;
						else if (packet == X86_USB_REPORT_USAGE_JOYSTICK)
							current_type = X86_USB_HID_TYPE_JOYSTICK;
						else if (packet == X86_USB_REPORT_USAGE_GAMEPAD)
							current_type = X86_USB_HID_TYPE_GAMEPAD;

						for (j = 0; j < 16; j++)
						{
							if (item_stats.usages[j] == 0)
							{
								item_stats.usages[j] = packet;
								break;
							}
						}
						
					} break;

					/* Uage Min & Max */
					case X86_USB_REPORT_LOCAL_TAG_USAGE_MIN:
					{
						item_stats.usage_min = packet;
					} break;
					case X86_USB_REPORT_LOCAL_TAG_USAGE_MAX:
					{
						item_stats.usage_max = packet;
					} break;

					/* Unhandled */
					default:
					{
						printf("%u: Local Item %u\n", depth, tag);
					} break;
				}
			} break;
		}
	}

	/* Save it in driver data */
	driver->collection = (root_collection == NULL) ? collection : root_collection;
}

/* Gives input data to an input item */
void usb_hid_apply_input_data(usb_hid_driver_t *driver_data, usb_report_collection_item_t *input_item)
{
	usb_report_input_t *input = (usb_report_input_t*)input_item->data;
	uint64_t value = 0, old_value = 0;
	uint32_t i, offset, length, usage;

	/* If we are constant, we are padding :D */
	if (input->flags == X86_USB_REPORT_INPUT_TYPE_CONSTANT)
		return;

	/* If we have a valid ReportID, make sure this data is for us */
	if (input_item->stats.report_id != 0xFFFFFFFF)
	{
		/* The first byte is the report Id */
		uint8_t report_id = driver_data->data_buffer[0];

		/* Sanity */
		if (report_id != (uint8_t)input_item->stats.report_id)
			return;
	}

	/* Loop through report data */
	offset = input->stats.bit_offset;
	length = input_item->stats.report_size;
	for (i = 0; i < input_item->stats.report_count; i++)
	{
		/* Get bits in question! */
		value = usb_get_bits(driver_data->data_buffer, offset, length);
		old_value = 0;
		
		/* We cant expect this to be correct though, it might be 0 */
		usage = input->stats.usages[i];

		/* Holy shit i hate switches */
		switch (input_item->stats.usage)
		{
			/* Mouse, Keyboard etc */
			case X86_USB_REPORT_USAGE_PAGE_GENERIC_PC:
			{
				/* Lets check sub-type (local usage) */
				switch (usage)
				{
					/* Calculating Device Bounds 
					 * Resolution = (Logical Maximum – Logical Minimum) / 
					 * ((Physical Maximum – Physical Minimum) * (10 Unit Exponent)) */

					/* If physical min/max is not defined or are 0, we set them to be logical min/max */

					/* X Axis Update */
					case X86_USB_REPORT_USAGE_X_AXIS:
					{
						int64_t x_relative = (int64_t)value;

						/* Convert to relative if necessary */
						if (input->flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							old_value = usb_get_bits(driver_data->previous_data_buffer, offset, length);
							x_relative = (int64_t)(value - old_value);
						}

						/* Fix-up relative number */
						if (x_relative > input_item->stats.log_max
							&& input_item->stats.log_min < 0)
						{
							/* This means we have to sign x_relative */
							x_relative = (int64_t)(~((uint64_t)x_relative));
							x_relative++;
						}

						if (x_relative != 0)
							printf("X-Change: %i (Original 0x%x, Old 0x%x)\n", 
								(int32_t)x_relative, (uint32_t)value, (uint32_t)old_value);

					} break;

					/* Y Axis Update */
					case X86_USB_REPORT_USAGE_Y_AXIS:
					{
						int64_t y_relative = (int64_t)value;

						/* Convert to relative if necessary */
						if (input->flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							old_value = usb_get_bits(driver_data->previous_data_buffer, offset, length);
							y_relative = (int64_t)(value - old_value);
						}

						/* Fix-up relative number */
						if (y_relative > input_item->stats.log_max
							&& input_item->stats.log_min < 0)
						{
							/* This means we have to sign x_relative */
							y_relative = (int64_t)(~((uint64_t)y_relative));
							y_relative++;
						}

						if (y_relative != 0)
							printf("Y-Change: %i (Original 0x%x, Old 0x%x)\n",
								(int32_t)y_relative, (uint32_t)value, (uint32_t)old_value);

					} break;

					/* Z Axis Update */
					case X86_USB_REPORT_USAGE_Z_AXIS:
					{
						int64_t z_relative = (int64_t)value;

						/* Convert to relative if necessary */
						if (input->flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							old_value = usb_get_bits(driver_data->previous_data_buffer, offset, length);
							z_relative = (int64_t)(value - old_value);
						}

						/* Fix-up relative number */
						if (z_relative > input_item->stats.log_max
							&& input_item->stats.log_min < 0)
						{
							/* This means we have to sign x_relative */
							z_relative = (int64_t)(~((uint64_t)z_relative));
							z_relative++;
						}

						if (z_relative != 0)
							printf("Z-Change: %i (Original 0x%x, Old 0x%x)\n",
								(int32_t)z_relative, (uint32_t)value, (uint32_t)old_value);

					} break;
				}

			} break;

			/* Buttons */
			case X86_USB_REPORT_USAGE_PAGE_BUTTON:
			{
				/* Determine if keystate has changed */
				uint8_t keystate_changed = 0;
				old_value = usb_get_bits(driver_data->previous_data_buffer, offset, length);

				if (value != old_value)
					keystate_changed = 1;

				/* Keyboard, keypad, mouse, gamepad or joystick? */
				switch (input_item->input_type)
				{
					/* Mouse Button */
					case X86_USB_HID_TYPE_MOUSE:
					{

					} break;

					/* Keyboard Button */
					case X86_USB_HID_TYPE_KEYBOARD:
					{

					} break;

					/* Keypad Button */
					case X86_USB_HID_TYPE_KEYPAD:
					{

					} break;

					/* Gamepad Button */
					case X86_USB_HID_TYPE_GAMEPAD:
					{

					} break;

					/* Joystick Button */
					case X86_USB_HID_TYPE_JOYSTICK:
					{

					} break;

					/* /Care */
					default:
						break;
				}

			} break;

			/* Debug Print */
			default:
			{
				/* What kind of hat is this ? */
				printf("Usage Page 0x%x (Input Type 0x%x), Usage 0x%x, Value 0x%x\n",
					input_item->stats.usage, input_item->input_type, usage, (uint32_t)value);
			} break;
		}

		/* Increase offset */
		offset += length;
	}
	

	/* printf("Input Item (%u): Report Offset %u, Report Size %u, Report Count %u (Minimum %i, Maximmum %i)\n",
		input->stats.usages[0], input->stats.bit_offset, input_item->stats.report_size, input_item->stats.report_count,
		input_item->stats.log_min, input_item->stats.log_max); */
}

/* Parses an subcollection, is recursive */
int usb_hid_apply_collection_data(usb_hid_driver_t *driver_data, usb_report_collection_t *collection)
{
	usb_report_collection_item_t *iterator = collection->childs;
	int calls = 0;
	
	/* Parse */
	while (iterator)
	{
		switch (iterator->type)
		{
			/* Sub Collection */
			case X86_USB_COLLECTION_TYPE_COLLECTION:
			{
				usb_report_collection_t *sub_collection 
					= (usb_report_collection_t*)iterator->data;

				/* Sanity */
				if (sub_collection == NULL)
					break;

				/* Recall */
				calls += usb_hid_apply_collection_data(driver_data, sub_collection);

			} break;

			/* Input */
			case X86_USB_COLLECTION_TYPE_INPUT:
			{
				/* Parse Input */
				usb_hid_apply_input_data(driver_data, iterator);

				/* Increase */
				calls++;

			} break;

			default:
				break;
		}

		/* Next Item */
		iterator = iterator->link;

		/* Sanity */
		if (iterator == NULL)
			break;
	}

	return calls;
}

/* The callback for device-feedback */
void usb_hid_callback(void *driver, size_t bytes)
{
	usb_hid_driver_t *driver_data = (usb_hid_driver_t*)driver;

	/* Sanity */
	if (driver_data->collection == NULL)
		return;

	/* Parse Collection (Recursively) */
	if (!usb_hid_apply_collection_data(driver_data, driver_data->collection))
	{
		printf("No calls were made...\n");
		return;
	}

	/* Now store this in old buffer */
	memcpy(driver_data->previous_data_buffer, driver_data->data_buffer, bytes);
}