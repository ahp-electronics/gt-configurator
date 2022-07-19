/*
 * Low-level DFU communication routines, originally taken from
 * $Id: dfu.c,v 1.3 2006/06/20 06:28:04 schmidtw Exp $
 * (part of dfu-programmer).
 *
 * Copyright 2005-2006 Weston Schmidt <weston_schmidt@alumni.purdue.edu>
 * Copyright 2011-2014 Tormod Volden <debian.tormod@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>
#include <unistd.h>

#include "portable.h"
#include "dfu.h"
#include "quirks.h"

static int dfu_timeout = 5000;  /* 5 seconds - default */

int verbose = 0;
dfu_if *dfu_root = NULL;
char *match_path = NULL;
int match_vendor = -1;
int match_product = -1;
int match_vendor_dfu = -1;
int match_product_dfu = -1;
int match_config_index = -1;
int match_iface_index = -1;
int match_iface_alt_index = -1;
int match_devnum = -1;
const char *match_iface_alt_name = NULL;
const char *match_serial = NULL;
const char *match_serial_dfu = NULL;

/*
 *  DFU_DETACH Request (DFU Spec 1.0, Section 5.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  timeout   - the timeout in ms the USB device should wait for a pending
 *              USB reset before giving up and terminating the operation
 *
 *  returns 0 or < 0 on error
 */
int dfu_detach( libusb_device_handle *device,
                const unsigned short interface,
                const unsigned short timeout )
{
    return libusb_control_transfer( device,
        /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        /* bRequest      */ DFU_DETACH,
        /* wValue        */ timeout,
        /* wIndex        */ interface,
        /* Data          */ NULL,
        /* wLength       */ 0,
                            dfu_timeout );
}


/*
 *  DFU_DNLOAD Request (DFU Spec 1.0, Section 6.1.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the total number of bytes to transfer to the USB
 *              device - must be less than wTransferSize
 *  data      - the data to transfer
 *
 *  returns the number of bytes written or < 0 on error
 */
int dfu_download( libusb_device_handle *device,
                  const unsigned short interface,
                  const unsigned short length,
                  const unsigned short transaction,
                  unsigned char* data )
{
    int status;

    status = libusb_control_transfer( device,
          /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
          /* bRequest      */ DFU_DNLOAD,
          /* wValue        */ transaction,
          /* wIndex        */ interface,
          /* Data          */ data,
          /* wLength       */ length,
                              dfu_timeout );
    return status;
}


/*
 *  DFU_UPLOAD Request (DFU Spec 1.0, Section 6.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the number of bytes received or < 0 on error
 */
int dfu_upload( libusb_device_handle *device,
                const unsigned short interface,
                const unsigned short length,
                const unsigned short transaction,
                unsigned char* data )
{
    int status;

    status = libusb_control_transfer( device,
          /* bmRequestType */ LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
          /* bRequest      */ DFU_UPLOAD,
          /* wValue        */ transaction,
          /* wIndex        */ interface,
          /* Data          */ data,
          /* wLength       */ length,
                              dfu_timeout );
    return status;
}


/*
 *  DFU_GETSTATUS Request (DFU Spec 1.0, Section 6.1.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  status    - the data structure to be populated with the results
 *
 *  return the number of bytes read in or < 0 on an error
 */
int dfu_get_status(dfu_if *dif, dfu_status *status )
{
    unsigned char buffer[6];
    int result;

    /* Initialize the status data structure */
    status->bStatus       = DFU_STATUS_ERROR_UNKNOWN;
    status->bwPollTimeout = 0;
    status->bState        = STATE_DFU_ERROR;
    status->iString       = 0;

    result = libusb_control_transfer( dif->dev_handle,
          /* bmRequestType */ LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
          /* bRequest      */ DFU_GETSTATUS,
          /* wValue        */ 0,
          /* wIndex        */ dif->interface,
          /* Data          */ buffer,
          /* wLength       */ 6,
                              dfu_timeout );

    if( 6 == result ) {
        status->bStatus = buffer[0];
        if (dif->quirks & QUIRK_POLLTIMEOUT)
            status->bwPollTimeout = DEFAULT_POLLTIMEOUT;
        else
            status->bwPollTimeout = ((0xff & buffer[3]) << 16) |
                                    ((0xff & buffer[2]) << 8)  |
                                    (0xff & buffer[1]);
        status->bState  = buffer[4];
        status->iString = buffer[5];
    }

    return result;
}


/*
 *  DFU_CLRSTATUS Request (DFU Spec 1.0, Section 6.1.3)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  return 0 or < 0 on an error
 */
int dfu_clear_status( libusb_device_handle *device,
                      const unsigned short interface )
{
    return libusb_control_transfer( device,
        /* bmRequestType */ LIBUSB_ENDPOINT_OUT| LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        /* bRequest      */ DFU_CLRSTATUS,
        /* wValue        */ 0,
        /* wIndex        */ interface,
        /* Data          */ NULL,
        /* wLength       */ 0,
                            dfu_timeout );
}


/*
 *  DFU_GETSTATE Request (DFU Spec 1.0, Section 6.1.5)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the state or < 0 on error
 */
int dfu_get_state( libusb_device_handle *device,
                   const unsigned short interface )
{
    int result;
    unsigned char buffer[1];

    result = libusb_control_transfer( device,
          /* bmRequestType */ LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
          /* bRequest      */ DFU_GETSTATE,
          /* wValue        */ 0,
          /* wIndex        */ interface,
          /* Data          */ buffer,
          /* wLength       */ 1,
                              dfu_timeout );

    /* Return the error if there is one. */
    if (result < 1)
	return -1;

    /* Return the state. */
    return buffer[0];
}


/*
 *  DFU_ABORT Request (DFU Spec 1.0, Section 6.1.4)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 or < 0 on an error
 */
int dfu_abort( libusb_device_handle *device,
               const unsigned short interface )
{
    return libusb_control_transfer( device,
        /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        /* bRequest      */ DFU_ABORT,
        /* wValue        */ 0,
        /* wIndex        */ interface,
        /* Data          */ NULL,
        /* wLength       */ 0,
                            dfu_timeout );
}


const char* dfu_state_to_string( int state )
{
    const char *message;

    switch (state) {
        case STATE_APP_IDLE:
            message = "appIDLE";
            break;
        case STATE_APP_DETACH:
            message = "appDETACH";
            break;
        case STATE_DFU_IDLE:
            message = "dfuIDLE";
            break;
        case STATE_DFU_DOWNLOAD_SYNC:
            message = "dfuDNLOAD-SYNC";
            break;
        case STATE_DFU_DOWNLOAD_BUSY:
            message = "dfuDNBUSY";
            break;
        case STATE_DFU_DOWNLOAD_IDLE:
            message = "dfuDNLOAD-IDLE";
            break;
        case STATE_DFU_MANIFEST_SYNC:
            message = "dfuMANIFEST-SYNC";
            break;
        case STATE_DFU_MANIFEST:
            message = "dfuMANIFEST";
            break;
        case STATE_DFU_MANIFEST_WAIT_RESET:
            message = "dfuMANIFEST-WAIT-RESET";
            break;
        case STATE_DFU_UPLOAD_IDLE:
            message = "dfuUPLOAD-IDLE";
            break;
        case STATE_DFU_ERROR:
            message = "dfuERROR";
            break;
        default:
            message = NULL;
            break;
    }

    return message;
}

/* Chapter 6.1.2 */
static const char *dfu_status_names[] = {
	/* DFU_STATUS_OK */
		"No error condition is present",
	/* DFU_STATUS_errTARGET */
		"File is not targeted for use by this device",
	/* DFU_STATUS_errFILE */
		"File is for this device but fails some vendor-specific test",
	/* DFU_STATUS_errWRITE */
		"Device is unable to write memory",
	/* DFU_STATUS_errERASE */
		"Memory erase function failed",
	/* DFU_STATUS_errCHECK_ERASED */
		"Memory erase check failed",
	/* DFU_STATUS_errPROG */
		"Program memory function failed",
	/* DFU_STATUS_errVERIFY */
		"Programmed memory failed verification",
	/* DFU_STATUS_errADDRESS */
		"Cannot program memory due to received address that is out of range",
	/* DFU_STATUS_errNOTDONE */
		"Received DFU_DNLOAD with wLength = 0, but device does not think that it has all data yet",
	/* DFU_STATUS_errFIRMWARE */
		"Device's firmware is corrupt. It cannot return to run-time (non-DFU) operations",
	/* DFU_STATUS_errVENDOR */
		"iString indicates a vendor specific error",
	/* DFU_STATUS_errUSBR */
		"Device detected unexpected USB reset signalling",
	/* DFU_STATUS_errPOR */
		"Device detected unexpected power on reset",
	/* DFU_STATUS_errUNKNOWN */
		"Something went wrong, but the device does not know what it was",
	/* DFU_STATUS_errSTALLEDPKT */
		"Device stalled an unexpected request"
};


const char *dfu_status_to_string(int status)
{
    if (status > DFU_STATUS_errSTALLEDPKT)
		return "INVALID";
	return dfu_status_names[status];
}

int dfu_abort_to_idle(dfu_if *dif)
{
	int ret;
    dfu_status dst;

	ret = dfu_abort(dif->dev_handle, dif->interface);
	if (ret < 0) {
		errx(EX_IOERR, "Error sending dfu abort request");
		exit(1);
	}
	ret = dfu_get_status(dif, &dst);
	if (ret < 0) {
		errx(EX_IOERR, "Error during abort get_status");
		exit(1);
	}
	if (dst.bState != DFU_STATE_dfuIDLE) {
		errx(EX_IOERR, "Failed to enter idle state on abort");
		exit(1);
	}
	milli_sleep(dst.bwPollTimeout);
	return ret;
}


int dfu_flash(const char *filename, int *progress, int *finished)
{
    dfu_status status;
    libusb_context *ctx;
    dfu_file file;
    memset(&file, 0, sizeof(file));
    int ret = libusb_init(&ctx);
    int transfer_size = 0;
    int func_dfu_transfer_size;
    if(dfu_root != NULL)
        free(dfu_root);
    dfu_root = NULL;
    *finished = 0;
    if (ret)
    {
        fprintf(stderr, "unable to initialize libusb: %s", libusb_error_name(ret));
        return EIO;
    }
    strcpy(file.name, filename);
    dfu_load_file(&file, MAYBE_SUFFIX, MAYBE_PREFIX);

    if (match_vendor < 0 && file.idVendor != 0xffff)
    {
        match_vendor = file.idVendor;
    }
    if (match_product < 0 && file.idProduct != 0xffff)
    {
        match_product = file.idProduct;
    }
    probe_devices(ctx);

    if (dfu_root == NULL)
    {
        ret = ENODEV;
        goto out;
    }
    else if (dfu_root->next != NULL)
    {
        /* We cannot safely support more than one DFU capable device
         * with same vendor/product ID, since during DFU we need to do
         * a USB bus reset, after which the target device will get a
         * new address */
        fprintf(stderr, "More than one DFU capable USB device found! "
                "Try `--list' and specify the serial number "
                "or disconnect all but one device\n");
        ret = ENODEV;
        goto out;
    }

    if (((file.idVendor  != 0xffff && file.idVendor  != dfu_root->vendor) ||
            (file.idProduct != 0xffff && file.idProduct != dfu_root->product)))
    {
        fprintf(stderr, "Error: File ID %04x:%04x does "
                "not match device (%04x:%04x)",
                file.idVendor, file.idProduct,
                dfu_root->vendor, dfu_root->product);
        ret = EINVAL;
        goto out;
    }

    ret = libusb_open(dfu_root->dev, &dfu_root->dev_handle);
    if (ret || !dfu_root->dev_handle)
        errx(EX_IOERR, "Cannot open device: %s", libusb_error_name(ret));

    ret = libusb_claim_interface(dfu_root->dev_handle, dfu_root->interface);
    if (ret < 0)
    {
        errx(EX_IOERR, "Cannot claim interface - %s", libusb_error_name(ret));
    }

    ret = libusb_set_interface_alt_setting(dfu_root->dev_handle, dfu_root->interface, dfu_root->altsetting);
    if (ret < 0)
    {
        errx(EX_IOERR, "Cannot set alternate interface: %s", libusb_error_name(ret));
    }

status_again:
    ret = dfu_get_status(dfu_root, &status );
    if (ret < 0)
    {
        fprintf(stderr, "error get_status: %s", libusb_error_name(ret));
    }

    usleep(status.bwPollTimeout * 1000);

    switch (status.bState)
    {
        case DFU_STATE_appIDLE:
        case DFU_STATE_appDETACH:
            fprintf(stderr, "Device still in Runtime Mode!");
            break;
        case DFU_STATE_dfuERROR:
            if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0)
            {
                fprintf(stderr, "error clear_status");
            }
            goto status_again;
            break;
        case DFU_STATE_dfuDNLOAD_IDLE:
        case DFU_STATE_dfuUPLOAD_IDLE:
            if (dfu_abort(dfu_root->dev_handle, dfu_root->interface) < 0)
            {
                fprintf(stderr, "can't send DFU_ABORT");
            }
            goto status_again;
            break;
        case DFU_STATE_dfuIDLE:
            break;
        default:
            break;
    }

    if (DFU_STATUS_OK != status.bStatus )
    {
        /* Clear our status & try again. */
        if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0)
            fprintf(stderr, "USB communication error");
        if (dfu_get_status(dfu_root, &status) < 0)
            fprintf(stderr, "USB communication error");
        if (DFU_STATUS_OK != status.bStatus)
            fprintf(stderr, "Status is not OK: %d", status.bStatus);

        usleep(status.bwPollTimeout * 1000);
    }

    func_dfu_transfer_size = libusb_le16_to_cpu(dfu_root->func_dfu.wTransferSize);
    if (func_dfu_transfer_size)
    {
        if (!transfer_size)
            transfer_size = func_dfu_transfer_size;
    }
    else
    {
        if (!transfer_size)
            fprintf(stderr, "Transfer size must be specified");
    }

    if (transfer_size < dfu_root->bMaxPacketSize0)
    {
        transfer_size = dfu_root->bMaxPacketSize0;
    }

    if (dfuload_do_dnload(dfu_root, transfer_size, &file, progress) < 0)
    {
        ret = EFAULT;
    }

    libusb_close(dfu_root->dev_handle);
    dfu_root->dev_handle = NULL;
out:
    libusb_exit(ctx);
    *finished = 1;
    return ret;
}
