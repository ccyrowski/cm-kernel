/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

#include <linux/msm_adsp.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/android_pmem.h>
#include <mach/msm_adsp.h>
#include "msm_vfe7x.h"

#define QDSP_CMDQUEUE QDSP_vfeCommandQueue

#define VFE_RESET_CMD 0
#define VFE_START_CMD 1
#define VFE_STOP_CMD  2
#define VFE_FRAME_ACK 20
#define STATS_AF_ACK  21
#define STATS_WE_ACK  22

#define MSG_SNAPSHOT  2
#define MSG_OUTPUT1   6
#define MSG_OUTPUT2   7
#define MSG_STATS_AF  8
#define MSG_STATS_WE  9

static struct msm_adsp_module *qcam_mod;
static struct msm_adsp_module *vfe_mod;
static struct msm_vfe_resp *resp;
static void *extdata;
static uint32_t extlen;

struct mutex vfe_lock;
static uint32_t vfe_inuse;
static void     *vfe_syncdata;

static void vfe_7x_convert(struct msm_vfe_phy_info *pinfo,
	enum vfe_resp_msg_t	type, void *data, void **ext, int32_t *elen)
{
	switch (type) {
	case VFE_MSG_OUTPUT1:
	case VFE_MSG_OUTPUT2: {
		pinfo->y_phy = ((struct vfe_endframe *)data)->y_address;
		pinfo->cbcr_phy =
			((struct vfe_endframe *)data)->cbcr_address;

		CDBG("vfe_7x_convert, y_phy = 0x%x, cbcr_phy = 0x%x\n",
				 pinfo->y_phy, pinfo->cbcr_phy);

		((struct vfe_frame_extra *)extdata)->bl_evencol =
		((struct vfe_endframe *)data)->blacklevelevencolumn;

		((struct vfe_frame_extra *)extdata)->bl_oddcol =
		((struct vfe_endframe *)data)->blackleveloddcolumn;

		((struct vfe_frame_extra *)extdata)->g_def_p_cnt =
		((struct vfe_endframe *)data)->greendefectpixelcount;

		((struct vfe_frame_extra *)extdata)->r_b_def_p_cnt =
		((struct vfe_endframe *)data)->redbluedefectpixelcount;

		*ext  = extdata;
		*elen = extlen;
	}
		break;

	case VFE_MSG_STATS_AF:
	case VFE_MSG_STATS_WE:
		pinfo->sbuf_phy = *(uint32_t *)data;
		break;

	default:
		break;
	} /* switch */
}

static void vfe_7x_ops(void *driver_data, unsigned id, size_t len,
		void (*getevent)(void *ptr, size_t len))
{
	uint32_t evt_buf[3];
	struct msm_vfe_resp_t *rp;

	rp = kmalloc(sizeof(struct msm_vfe_resp_t), GFP_ATOMIC);
	if (!rp) {
		pr_err("rp: cannot allocate buffer\n");
		return;
	}

	if (id == ((uint16_t)-1)) {
		/* event */
		getevent(evt_buf, sizeof(evt_buf));

		rp->type           = VFE_EVENT;
		rp->evt_msg.type   = MSM_CAMERA_EVT;
		rp->evt_msg.len    = 0;
		rp->evt_msg.msg_id = evt_buf[0];

		resp->vfe_resp(rp, MSM_CAM_Q_VFE_EVT, vfe_syncdata);
	} else {
		/* messages */

		rp->evt_msg.type   = MSM_CAMERA_MSG;
		rp->evt_msg.msg_id = id;
		rp->evt_msg.len    = len;

		rp->evt_msg.data = kmalloc(rp->evt_msg.len, GFP_ATOMIC);
		if (!(rp->evt_msg.data)) {
			kfree(rp);
			pr_err("rp->evt_msg.data: cannot allocate buffer\n");
			return;
		}

		getevent(rp->evt_msg.data, len);

		switch (rp->evt_msg.msg_id) {
		case MSG_SNAPSHOT:
			rp->type = VFE_MSG_SNAPSHOT;
			break;

		case MSG_OUTPUT1:
			rp->type = VFE_MSG_OUTPUT1;
			vfe_7x_convert(&(rp->phy), VFE_MSG_OUTPUT1,
				rp->evt_msg.data, &(rp->extdata),
				&(rp->extlen));
			break;

		case MSG_OUTPUT2:
			rp->type = VFE_MSG_OUTPUT2;
			vfe_7x_convert(&(rp->phy), VFE_MSG_OUTPUT2,
					rp->evt_msg.data, &(rp->extdata),
					&(rp->extlen));
			break;

		case MSG_STATS_AF:
			rp->type = VFE_MSG_STATS_AF;
			vfe_7x_convert(&(rp->phy), VFE_MSG_STATS_AF,
					rp->evt_msg.data, NULL, NULL);
			break;

		case MSG_STATS_WE:
			rp->type = VFE_MSG_STATS_WE;
			vfe_7x_convert(&(rp->phy), VFE_MSG_STATS_WE,
					rp->evt_msg.data, NULL, NULL);

			CDBG("MSG_STATS_WE: phy = 0x%x\n", rp->phy.sbuf_phy);
			break;

		default:
			rp->type = VFE_MSG_GENERAL;
			break;
		}

		resp->vfe_resp(rp, MSM_CAM_Q_VFE_MSG, vfe_syncdata);
	}
}

static struct msm_adsp_ops vfe_7x_sync = {
	.event = vfe_7x_ops,
};

static int vfe_7x_enable(struct camera_enable_cmd_t *enable)
{
	int rc = -EFAULT;

	if (!strcmp(enable->name, "QCAMTASK"))
		rc = msm_adsp_enable(qcam_mod);
	else if (!strcmp(enable->name, "VFETASK"))
		rc = msm_adsp_enable(vfe_mod);

	return rc;
}

static int vfe_7x_disable(struct camera_enable_cmd_t *enable,
	struct platform_device *dev)
{
	int rc = -EFAULT;

	if (!strcmp(enable->name, "QCAMTASK"))
		rc = msm_adsp_disable(qcam_mod);
	else if (!strcmp(enable->name, "VFETASK"))
		rc = msm_adsp_disable(vfe_mod);

	return rc;
}

static void vfe_7x_release(struct platform_device *dev)
{
	mutex_lock(&vfe_lock);
	vfe_inuse = 0;
	vfe_syncdata = NULL;
	mutex_unlock(&vfe_lock);

	msm_adsp_disable(qcam_mod);
	msm_adsp_disable(vfe_mod);

	msm_adsp_put(qcam_mod);
	msm_adsp_put(vfe_mod);

	msm_camio_disable(dev);

	kfree(extdata);
	extlen = 0;
}

static int vfe_7x_init(struct msm_vfe_resp *presp,
	struct platform_device *dev)
{
	int rc = 0;

	if (presp && presp->vfe_resp)
		resp = presp;
	else
		return -EFAULT;

	/* Bring up all the required GPIOs and Clocks */
	rc = msm_camio_enable(dev);
	if (rc < 0)
		return rc;

	msm_camio_camif_pad_reg_reset();

	extlen = sizeof(struct vfe_frame_extra);

	extdata =
		kmalloc(sizeof(extlen), GFP_ATOMIC);
	if (!extdata) {
		rc = -ENOMEM;
		goto init_fail;
	}

	rc = msm_adsp_get("QCAMTASK", &qcam_mod, &vfe_7x_sync, NULL);
	if (rc) {
		rc = -EBUSY;
		goto get_qcam_fail;
	}

	rc = msm_adsp_get("VFETASK", &vfe_mod, &vfe_7x_sync, NULL);
	if (rc) {
		rc = -EBUSY;
		goto get_vfe_fail;
	}

	return 0;

get_vfe_fail:
	msm_adsp_put(qcam_mod);
get_qcam_fail:
	kfree(extdata);
init_fail:
	extlen = 0;
	return rc;
}

static int vfe_7x_config_axi(enum vfeoutput_mode_t mode,
	struct axidata_t *ad, struct axiout_t *ao)
{
	struct msm_pmem_region *regptr;
	unsigned long *bptr;
	int    cnt;

	int rc = 0;

	if (mode == OUTPUT_1 || mode == OUTPUT_1_AND_2) {
		regptr = ad->region;

		CDBG("bufnum1 = %d\n", ad->bufnum1);
		CDBG("config_axi1: O1, phy = 0x%lx, y_off = %d, cbcr_off =%d\n",
			regptr->paddr, regptr->y_off, regptr->cbcr_off);

		bptr = &ao->output1buffer1_y_phy;
		for (cnt = 0; cnt < ad->bufnum1; cnt++) {
			*bptr = regptr->paddr + regptr->y_off;
			bptr++;
			*bptr = regptr->paddr + regptr->cbcr_off;

			bptr++;
			regptr++;
		}

		regptr--;
		for (cnt = 0; cnt < (8 - ad->bufnum1); cnt++) {
			*bptr = regptr->paddr + regptr->y_off;
			bptr++;
			*bptr = regptr->paddr + regptr->cbcr_off;
			bptr++;
		}
	} /* if OUTPUT1 or Both */

	if (mode == OUTPUT_2 || mode == OUTPUT_1_AND_2) {
		regptr = &(ad->region[ad->bufnum1]);

		CDBG("bufnum2 = %d\n", ad->bufnum2);
		CDBG("config_axi2: O2, phy = 0x%lx, y_off = %d, cbcr_off =%d\n",
			regptr->paddr, regptr->y_off, regptr->cbcr_off);

		bptr = &ao->output2buffer1_y_phy;
		for (cnt = 0; cnt < ad->bufnum2; cnt++) {
			*bptr = regptr->paddr + regptr->y_off;
			bptr++;
			*bptr = regptr->paddr + regptr->cbcr_off;

			bptr++;
			regptr++;
		}

		regptr--;
		for (cnt = 0; cnt < (8 - ad->bufnum2); cnt++) {
			*bptr = regptr->paddr + regptr->y_off;
			bptr++;
			*bptr = regptr->paddr + regptr->cbcr_off;
			bptr++;
		}
	}

	return rc;
}

static int vfe_7x_config(struct msm_vfe_cfg_cmd_t *cmd, void *data)
{
	struct msm_pmem_region *regptr;
	unsigned char buf[256];

	struct vfe_stats_ack_t sack;
	struct axidata_t *axid;
	uint32_t i;

	struct vfe_stats_we_cfg_t *scfg_t = NULL;
	struct vfe_stats_af_cfg_t *sfcfg_t = NULL;

	struct axiout_t *axio = NULL;
	void   *cmd_data = NULL;
	void   *cmd_data_alloc = NULL;
	long rc = 0;
	struct msm_vfe_command_7k *vfecmd;

	static uint8_t vfestopped;

	vfecmd =
			kmalloc(sizeof(struct msm_vfe_command_7k),
				GFP_ATOMIC);
	if (!vfecmd) {
		pr_err("vfecmd alloc failed!\n");
		return -ENOMEM;
	}

	if (cmd->cmd_type != CMD_FRAME_BUF_RELEASE &&
	    cmd->cmd_type != CMD_STATS_BUF_RELEASE &&
	    cmd->cmd_type != CMD_STATS_AF_BUF_RELEASE) {
		if (copy_from_user(vfecmd,
				(void __user *)(cmd->value),
				sizeof(struct msm_vfe_command_7k))) {
			rc = -EFAULT;
			goto config_failure;
		}
	}

	switch (cmd->cmd_type) {
	case CMD_STATS_ENABLE:
	case CMD_STATS_AXI_CFG: {
		axid = data;
		if (!axid) {
			rc = -EFAULT;
			goto config_failure;
		}

		scfg_t =
			kmalloc(sizeof(struct vfe_stats_we_cfg_t),
				GFP_ATOMIC);
		if (!scfg_t) {
			rc = -ENOMEM;
			goto config_failure;
		}

		if (copy_from_user(scfg_t,
					(void __user *)(vfecmd->value),
					vfecmd->length)) {

			rc = -EFAULT;
			goto config_done;
		}

		CDBG("STATS_ENABLE: bufnum = %d, enabling = %d\n",
			axid->bufnum1, scfg_t->wb_expstatsenable);

		if (axid->bufnum1 > 0) {
			regptr = axid->region;

			for (i = 0; i < axid->bufnum1; i++) {

				CDBG("STATS_ENABLE, phy = 0x%lx\n",
					regptr->paddr);

				scfg_t->wb_expstatoutputbuffer[i] =
					(void *)regptr->paddr;
				regptr++;
			}

			cmd_data = scfg_t;

		} else {
			rc = -EINVAL;
			goto config_done;
		}
	}
		break;

	case CMD_STATS_AF_ENABLE:
	case CMD_STATS_AF_AXI_CFG: {
		axid = data;
		if (!axid) {
			rc = -EFAULT;
			goto config_failure;
		}

		sfcfg_t =
			kmalloc(sizeof(struct vfe_stats_af_cfg_t),
				GFP_ATOMIC);

		if (!sfcfg_t) {
			rc = -ENOMEM;
			goto config_failure;
		}

		if (copy_from_user(sfcfg_t,
					(void __user *)(vfecmd->value),
					vfecmd->length)) {

			rc = -EFAULT;
			goto config_done;
		}

		CDBG("AF_ENABLE: bufnum = %d, enabling = %d\n",
			axid->bufnum1, sfcfg_t->af_enable);

		if (axid->bufnum1 > 0) {
			regptr = axid->region;

			for (i = 0; i < axid->bufnum1; i++) {

				CDBG("STATS_ENABLE, phy = 0x%lx\n",
					regptr->paddr);

				sfcfg_t->af_outbuf[i] =
					(void *)regptr->paddr;

				regptr++;
			}

			cmd_data = sfcfg_t;

		} else {
			rc = -EINVAL;
			goto config_done;
		}
	}
		break;

	case CMD_FRAME_BUF_RELEASE: {
		struct msm_frame_t *b;
		unsigned long p;
		struct vfe_outputack_t fack;
		if (!data)  {
			rc = -EFAULT;
			goto config_failure;
		}

		b = (struct msm_frame_t *)(cmd->value);
		p = *(unsigned long *)data;

		fack.header = VFE_FRAME_ACK;

		fack.output2newybufferaddress =
			(void *)(p + b->y_off);

		fack.output2newcbcrbufferaddress =
			(void *)(p + b->cbcr_off);

		vfecmd->queue = QDSP_CMDQUEUE;
		vfecmd->length = sizeof(struct vfe_outputack_t);
		cmd_data = &fack;
	}
		break;

	case CMD_SNAP_BUF_RELEASE:
		break;

	case CMD_STATS_BUF_RELEASE: {
		CDBG("vfe_7x_config: CMD_STATS_BUF_RELEASE\n");
		if (!data) {
			rc = -EFAULT;
			goto config_failure;
		}

		sack.header = STATS_WE_ACK;
		sack.bufaddr = (void *)*(uint32_t *)data;

		vfecmd->queue  = QDSP_CMDQUEUE;
		vfecmd->length = sizeof(struct vfe_stats_ack_t);
		cmd_data = &sack;
	}
		break;

	case CMD_STATS_AF_BUF_RELEASE: {
		CDBG("vfe_7x_config: CMD_STATS_AF_BUF_RELEASE\n");
		if (!data) {
			rc = -EFAULT;
			goto config_failure;
		}

		sack.header = STATS_AF_ACK;
		sack.bufaddr = (void *)*(uint32_t *)data;

		vfecmd->queue  = QDSP_CMDQUEUE;
		vfecmd->length = sizeof(struct vfe_stats_ack_t);
		cmd_data = &sack;
	}
		break;

	case CMD_GENERAL:
	case CMD_STATS_DISABLE: {
		if (vfecmd->length > 256) {
			cmd_data_alloc =
			cmd_data = kmalloc(vfecmd->length, GFP_ATOMIC);
			if (!cmd_data) {
				rc = -ENOMEM;
				goto config_failure;
			}
		} else
			cmd_data = buf;

		if (copy_from_user(cmd_data,
					(void __user *)(vfecmd->value),
					vfecmd->length)) {

			rc = -EFAULT;
			goto config_done;
		}

		if (vfecmd->queue == QDSP_CMDQUEUE) {
			switch (*(uint32_t *)cmd_data) {
			case VFE_RESET_CMD:
				msm_camio_vfe_blk_reset();
				msm_camio_camif_pad_reg_reset_2();
				vfestopped = 0;
				break;

			case VFE_START_CMD:
				msm_camio_camif_pad_reg_reset_2();
				vfestopped = 0;
				break;

			case VFE_STOP_CMD:
				vfestopped = 1;
				goto config_send;

			default:
				break;
			}
		} /* QDSP_CMDQUEUE */
	}
		break;

	case CMD_AXI_CFG_OUT1: {
		axid = data;
		if (!axid) {
			rc = -EFAULT;
			goto config_failure;
		}

		axio = kmalloc(sizeof(struct axiout_t), GFP_ATOMIC);
		if (!axio) {
			rc = -ENOMEM;
			goto config_failure;
		}

		if (copy_from_user(axio, (void *)(vfecmd->value),
					sizeof(struct axiout_t))) {
			rc = -EFAULT;
			goto config_done;
		}

		vfe_7x_config_axi(OUTPUT_1, axid, axio);

		cmd_data = axio;
	}
		break;

	case CMD_AXI_CFG_OUT2:
	case CMD_RAW_PICT_AXI_CFG: {
		axid = data;
		if (!axid) {
			rc = -EFAULT;
			goto config_failure;
		}

		axio = kmalloc(sizeof(struct axiout_t), GFP_ATOMIC);
		if (!axio) {
			rc = -ENOMEM;
			goto config_failure;
		}

		if (copy_from_user(axio, (void __user *)(vfecmd->value),
					sizeof(struct axiout_t))) {
			rc = -EFAULT;
			goto config_done;
		}

		vfe_7x_config_axi(OUTPUT_2, axid, axio);
		cmd_data = axio;
	}
		break;

	case CMD_AXI_CFG_SNAP_O1_AND_O2: {
		axid = data;
		if (!axid) {
			rc = -EFAULT;
			goto config_failure;
		}

		axio = kmalloc(sizeof(struct axiout_t), GFP_ATOMIC);
		if (!axio) {
			rc = -ENOMEM;
			goto config_failure;
		}

		if (copy_from_user(axio, (void __user *)(vfecmd->value),
					sizeof(struct axiout_t))) {
			rc = -EFAULT;
			goto config_done;
		}

		vfe_7x_config_axi(OUTPUT_1_AND_2, axid, axio);

		cmd_data = axio;
	}
		break;

	default:
		break;
	} /* switch */

	if (vfestopped)
		goto config_done;

config_send:
	CDBG("send adsp command = %d\n", *(uint32_t *)cmd_data);
	rc = msm_adsp_write(vfe_mod, vfecmd->queue,
				cmd_data, vfecmd->length);

config_done:
	if (cmd_data_alloc != NULL)
		kfree(cmd_data_alloc);

config_failure:
	kfree(scfg_t);
	kfree(axio);
	kfree(vfecmd);
	return rc;
}

void msm_camvfe_fn_init(struct msm_camvfe_fn_t *fptr)
{
	fptr->vfe_init    = vfe_7x_init;
	fptr->vfe_enable  = vfe_7x_enable;
	fptr->vfe_config  = vfe_7x_config;
	fptr->vfe_disable = vfe_7x_disable;
	fptr->vfe_release = vfe_7x_release;
}

int msm_camvfe_check(void *data)
{
	int rc = 0;

	mutex_lock(&vfe_lock);
	if (!vfe_inuse) {
		vfe_inuse = 1;
		vfe_syncdata = data;
	} else if (data != vfe_syncdata) {
		rc = -EBUSY;
	}
	mutex_unlock(&vfe_lock);

	return rc;
}

void msm_camvfe_init(void)
{
	mutex_init(&vfe_lock);
	vfe_inuse = 0;
	vfe_syncdata = NULL;
}
