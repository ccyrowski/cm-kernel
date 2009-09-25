/* drivers/usb/function/usb_function.h
 *
 * USB Function Device Interface
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DRIVERS_USB_FUNCTION_USB_FUNCTION_H_
#define _DRIVERS_USB_FUNCTION_USB_FUNCTION_H_

#include <linux/list.h>
#include <linux/usb/ch9.h>

#define EPT_BULK_IN   1
#define EPT_BULK_OUT  2

struct usb_endpoint;

struct usb_request
{
	void *buf;          /* pointer to associated data buffer */
	unsigned length;    /* requested transfer length */
	int status;         /* status upon completion */
	unsigned actual;    /* actual bytes transferred */

	void (*complete)(struct usb_endpoint *ep, struct usb_request *req);
	void *context;

	struct list_head list;
};

struct usb_function
{
	/* bind() is called once when the function has had its endpoints
	** allocated, but before the bus is active.
	**
	** might be a good place to allocate some usb_request objects
	*/
	void (*bind)(struct usb_endpoint **ept, void *context);

	/* unbind() is called when the function is being removed.
	** it is illegal to call and usb_ept_* hooks at this point
	** and all endpoints must be released.
	*/
	void (*unbind)(void *context);

	/* configure() is called when the usb client has been configured
	** by the host and again when the device is unconfigured (or
	** when the client is detached)
	**
	** currently called from interrupt context.
	*/
	void (*configure)(int configured, void *context);

	/* setup() is called to allow functions to handle class and vendor
	** setup requests.  If the request is unsupported or can not be handled,
	** setup() should return -1.
	** For OUT requests, buf will point to a buffer to data received in the
	** request's data phase, and len will contain the length of the data.
	** setup() should return 0 after handling an OUT request successfully.
	** for IN requests, buf will contain a pointer to a buffer for setup()
	** to write data to, and len will be the maximum size of the data to
	** be written back to the host.
	** After successfully handling an IN request, setup() should return
	** the number of bytes written to buf that should be sent in the
	** response to the host.
	*/
	int (*setup)(struct usb_ctrlrequest *req, void *buf,
			int len, void *context);

	/* driver name */
	const char *name;
	void *context;

	/* interface class/subclass/protocol for descriptor */
	unsigned char ifc_class;
	unsigned char ifc_subclass;
	unsigned char ifc_protocol;

	/* name string for descriptor */
	const char *ifc_name;

	/* number of needed endpoints and their types */
	unsigned char ifc_ept_count;
	unsigned char ifc_ept_type[8];

	/* if the endpoint is disabled, its interface will not be
	** included in the configuration descriptor
	*/
	unsigned char   disabled;
};

int usb_function_register(struct usb_function *driver);

void usb_function_enable(const char *function, int enable);

/* Allocate a USB request.
** Must be called from a context that can sleep.
** If bufsize is nonzero, req->buf will be allocated for
** you and free'd when the request is free'd.  Otherwise
** it is your responsibility to provide.
*/
struct usb_request *usb_ept_alloc_req(struct usb_endpoint *ept, unsigned bufsize);
void usb_ept_free_req(struct usb_endpoint *ept, struct usb_request *req);

/* safely callable from any context
** returns 0 if successfully queued and sets req->status = -EBUSY
** req->status will change to a different value upon completion
** (0 for success, -EIO, -ENODEV, etc for error)
*/
int usb_ept_queue_xfer(struct usb_endpoint *ept, struct usb_request *req);
int usb_ept_flush(struct usb_endpoint *ept);
int usb_ept_get_max_packet(struct usb_endpoint *ept);
#endif
