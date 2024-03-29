/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_linux.c 324874 2012-03-30 18:29:52Z $
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/ioprio.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif

#ifdef WLBTAMP
#include <proto/802.11_bta.h>
#include <proto/bt_amp_hci.h>
#include <dhd_bta.h>
#endif

extern int bcm_chip_is_4330b1;
extern int bcm_chip_is_4330;
dhd_pub_t *priv_dhdp = NULL;

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
static int module_insert = 0;
int module_remove = 0; 

#ifdef WLMEDIA_HTSF
#include <linux/time.h>
#include <htsf.h>

#define HTSF_MINLEN 200    
#define HTSF_BUS_DELAY 150 
#define TSMAX  1000        
#define NUMBIN 34

static uint32 tsidx = 0;
static uint32 htsf_seqnum = 0;
uint32 tsfsync;
struct timeval tsync;
static uint32 tsport = 5010;

typedef struct histo_ {
	uint32 bin[NUMBIN];
} histo_t;

#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

static histo_t vi_d1, vi_d2, vi_d3, vi_d4;
#endif 

#ifndef DTIM_COUNT
#define DTIM_COUNT	3
#endif

#define HTC_DUMP_PACKLEN 0

#if defined(PKT_FILTER_SUPPORT)
#if defined(BLOCK_IPV6_PACKET)
#define HEX_PREF_STR	"0x"
#define UNI_FILTER_STR	"010000000000"
#define ZERO_ADDR_STR	"000000000000"
#define ETHER_TYPE_STR	"0000"
#define IPV6_FILTER_STR	"20"
#define ZERO_TYPE_STR	"00"
#endif 
#endif 

#if defined(SOFTAP)
extern bool ap_cfg_running;
extern bool ap_fw_loaded;
#endif
bool wifi_fail_retry = false;

#define AOE_IP_ALIAS_SUPPORT 1

#ifdef BCM_FD_AGGR
#include <bcm_rpc.h>
#include <bcm_rpc_tp.h>
#endif
#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#include <wl_android.h>

#ifdef ARP_OFFLOAD_SUPPORT
void aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add);
static int dhd_device_event(struct notifier_block *this,
	unsigned long event,
	void *ptr);

static struct notifier_block dhd_notifier = {
	.notifier_call = dhd_device_event
};
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif 

#if defined(OOB_INTR_ONLY)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (1)
static void dhd_hang_process(struct work_struct *work);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
MODULE_LICENSE("GPL v2");
#endif 

#include <dhd_bus.h>

#ifdef BCM_FD_AGGR
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(BCM_RPC_TP_DNGL_AGG_MAX_BYTE)
#else
#ifndef PROP_TXSTATUS
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen)
#else
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen + 128)
#endif
#endif 

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15)
const char *
print_tainted()
{
	return "";
}
#endif	

#if defined(CONFIG_WIRELESS_EXT)
#include <wl_iw.h>
extern wl_iw_extra_params_t  g_wl_iw_params;
#endif 

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);
extern int dhd_get_dtim_skip(dhd_pub_t *dhd);
#endif 

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg);
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif

#ifdef READ_MACADDR
extern int dhd_read_macaddr(struct dhd_info *dhd, struct ether_addr *mac);
#endif
#ifdef RDWR_MACADDR
extern int dhd_check_rdwr_macaddr(struct dhd_info *dhd, dhd_pub_t *dhdp, struct ether_addr *mac);
extern int dhd_write_rdwr_macaddr(struct ether_addr *mac);
#endif
#ifdef WRITE_MACADDR
extern int dhd_write_macaddr(struct ether_addr *mac);
#endif
#ifdef USE_CID_CHECK
extern int dhd_check_module_cid(dhd_pub_t *dhd);
#endif
#ifdef GET_MAC_FROM_OTP
extern int dhd_check_module_mac(dhd_pub_t *dhd);
#endif
#ifdef MIMO_ANT_SETTING
extern int dhd_sel_ant_from_file(dhd_pub_t *dhd);
#endif

#ifdef CUSTOMER_SET_COUNTRY
int dhd_customer_set_country(dhd_pub_t *dhd);
#endif

typedef struct dhd_if {
	struct dhd_info *info;			
	
	struct net_device *net;
	struct net_device_stats stats;
	int 			idx;			
	dhd_if_state_t	state;			
	uint 			subunit;		
	uint8			mac_addr[ETHER_ADDR_LEN];	
	bool			attached;		
	bool			txflowcontrol;	
	char			name[IFNAMSIZ+1]; 
	uint8			bssidx;			
	bool			set_multicast;
	bool			event2cfg80211;	
} dhd_if_t;

#ifdef WLMEDIA_HTSF
typedef struct {
	uint32 low;
	uint32 high;
} tsf_t;

typedef struct {
	uint32 last_cycle;
	uint32 last_sec;
	uint32 last_tsf;
	uint32 coef;     
	uint32 coefdec1; 
	uint32 coefdec2; 
} htsf_t;

typedef struct {
	uint32 t1;
	uint32 t2;
	uint32 t3;
	uint32 t4;
} tstamp_t;

static tstamp_t ts[TSMAX];
static tstamp_t maxdelayts;
static uint32 maxdelay = 0, tspktcnt = 0, maxdelaypktno = 0;

#endif  

#if defined(CUSTOMER_HW4) && defined(CONFIG_PM_SLEEP) && defined(PLATFORM_SLP)
extern struct device *pm_dev;
#endif 

typedef struct dhd_info {
#if defined(CONFIG_WIRELESS_EXT)
	wl_iw_t		iw;		
#endif 

	dhd_pub_t pub;

	
	dhd_if_t *iflist[DHD_MAX_IFS];

	struct semaphore proto_sem;
#ifdef PROP_TXSTATUS
	spinlock_t	wlfc_spinlock;
#endif 
#ifdef WLMEDIA_HTSF
	htsf_t  htsf;
#endif
	wait_queue_head_t ioctl_resp_wait;
	struct timer_list timer;
	bool wd_timer_valid;
	struct tasklet_struct tasklet;
	spinlock_t	sdlock;
	spinlock_t	txqlock;
	spinlock_t	dhd_lock;
#ifdef DHDTHREAD
	
	bool threads_only;
	struct semaphore sdsem;

	tsk_ctl_t	thr_dpc_ctl;
	tsk_ctl_t	thr_wdt_ctl;
#endif 
	bool dhd_tasklet_create;
	tsk_ctl_t	thr_sysioc_ctl;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	struct work_struct work_hang;
#endif

	
#if defined(CONFIG_HAS_WAKELOCK) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	struct wake_lock wl_wifi;   
	struct wake_lock wl_rxwake; 
	struct wake_lock wl_ctrlwake; 
	struct wake_lock wl_htc; 	
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	struct mutex dhd_net_if_mutex;
	struct mutex dhd_suspend_mutex;
#endif
	spinlock_t wakelock_spinlock;
	int wakelock_counter;
	int wakelock_rx_timeout_enable;
	int wakelock_ctrl_timeout_enable;

	
	unsigned char set_macaddress;
	struct ether_addr macvalue;
	wait_queue_head_t ctrl_wait;
	atomic_t pend_8021x_cnt;
	dhd_attach_states_t dhd_state;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 

#ifdef ARP_OFFLOAD_SUPPORT
	u32 pend_ipaddr;
#endif 
    bool dhd_force_exit; 
#ifdef BCM_FD_AGGR
	void *rpc_th;
	void *rpc_osh;
	struct timer_list rpcth_timer;
	bool rpcth_timer_active;
	bool fdaggr;
#endif
} dhd_info_t;


char firmware_path[MOD_PARAM_PATHLEN];
char fwb1_path[MOD_PARAM_PATHLEN];
char fwb2_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];

char info_string[MOD_PARAM_INFOLEN];
module_param_string(info_string, info_string, MOD_PARAM_INFOLEN, 0444);

int op_mode = 0;
int disable_proptx = 0;
module_param(op_mode, int, 0644);
extern int wl_control_wl_start(struct net_device *dev);
extern int net_os_send_hang_message(struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
struct semaphore dhd_registration_sem;
struct semaphore dhd_chipup_sem;
int dhd_registration_check = FALSE;

#define DHD_REGISTRATION_TIMEOUT  12000  
#endif 

uint dhd_sysioc = TRUE;
module_param(dhd_sysioc, uint, 0);

module_param(dhd_msg_level, int, 0);

module_param(disable_proptx, int, 0644);
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0660);
module_param_string(fwb1_path, fwb1_path, MOD_PARAM_PATHLEN, 0);
module_param_string(fwb2_path, fwb2_path, MOD_PARAM_PATHLEN, 0);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0);

uint dhd_watchdog_ms = 10;
module_param(dhd_watchdog_ms, uint, 0);

#if defined(DHD_DEBUG)
uint dhd_console_ms = 0;
module_param(dhd_console_ms, uint, 0644);
#endif 



uint dhd_slpauto = TRUE;
module_param(dhd_slpauto, uint, 0);

uint dhd_arp_mode = 0xb;
module_param(dhd_arp_mode, uint, 0);

uint dhd_arp_enable = TRUE;
module_param(dhd_arp_enable, uint, 0);

#ifdef PKT_FILTER_SUPPORT
uint dhd_pkt_filter_enable = TRUE;
module_param(dhd_pkt_filter_enable, uint, 0);
#endif

uint dhd_pkt_filter_init = 0;
module_param(dhd_pkt_filter_init, uint, 0);

#ifdef GAN_LITE_NAT_KEEPALIVE_FILTER
uint dhd_master_mode = FALSE;
#else
uint dhd_master_mode = TRUE;
#endif 
module_param(dhd_master_mode, uint, 0);

#ifdef DHDTHREAD
int dhd_watchdog_prio = 102;
module_param(dhd_watchdog_prio, int, 0);

int dhd_dpc_prio = 103;
module_param(dhd_dpc_prio, int, 0);

extern int dhd_dongle_memsize;
module_param(dhd_dongle_memsize, int, 0);
#endif 
#ifdef CUSTOMER_HW2
uint dhd_roam_disable = 0;
#else
uint dhd_roam_disable = 1;
#endif

uint dhd_radio_up = 1;

char iface_name[IFNAMSIZ] = {'\0'};
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else 
#define RAISE_RX_SOFTIRQ() \
	cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt())
#endif


int dhd_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

int dhd_idletime = DHD_IDLETIME_TICKS;
module_param(dhd_idletime, int, 0);

uint dhd_poll = FALSE;
module_param(dhd_poll, uint, 0);

uint dhd_intr = TRUE;
module_param(dhd_intr, uint, 0);

uint dhd_sdiod_drive_strength = 12;
module_param(dhd_sdiod_drive_strength, uint, 0);

extern uint dhd_txbound;
extern uint dhd_rxbound;
module_param(dhd_txbound, uint, 0);
module_param(dhd_rxbound, uint, 0);

extern uint dhd_deferred_tx;
module_param(dhd_deferred_tx, uint, 0);

#ifdef BCMDBGFS
extern void dhd_dbg_init(dhd_pub_t *dhdp);
extern void dhd_dbg_remove(void);
#endif 



#ifdef SDTEST
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif 

#ifdef DHD_DEBUG
#ifndef SRCBASE
#define SRCBASE        "drivers/net/wireless/bcmdhd"
#endif
#define DHD_COMPILED "\nCompiled in " SRCBASE
#else
#define DHD_COMPILED
#endif 

static char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR
#ifdef DHD_DEBUG
"\nCompiled in " SRCBASE " on " __DATE__ " at " __TIME__
#endif
;
static void dhd_net_if_lock_local(dhd_info_t *dhd);
static void dhd_net_if_unlock_local(dhd_info_t *dhd);
#if 0
static void dhd_suspend_lock(dhd_pub_t *dhdp);
static void dhd_suspend_unlock(dhd_pub_t *dhdp);
#endif

#ifdef WLMEDIA_HTSF
void htsf_update(dhd_info_t *dhd, void *data);
tsf_t prev_tsf, cur_tsf;

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx);
static int dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx);
static void dhd_dump_latency(void);
static void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_dump_htsfhisto(histo_t *his, char *s);
#endif 

int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);

#ifdef CONFIG_CONTROL_PM
bool g_pm_control;
void sec_control_pm(dhd_pub_t *dhd, uint *);
#endif

#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif 

static void dhd_dpc(ulong data);
extern int dhd_wait_pend8021x(struct net_device *dev);

#ifdef TOE
#ifndef BDC
#error TOE requires BDC
#endif 
static int dhd_toe_get(dhd_info_t *dhd, int idx, uint32 *toe_ol);
static int dhd_toe_set(dhd_info_t *dhd, int idx, uint32 toe_ol);
#endif 

static int dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
                             wl_event_msg_t *event_ptr, void **data_ptr);

#define WLC_HT_TKIP_RESTRICT    0x02     
#define WLC_HT_WEP_RESTRICT     0x01    

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
static int dhd_sleep_pm_callback(struct notifier_block *nfb, unsigned long action, void *ignored)
{
	int ret = NOTIFY_DONE;

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		dhd_mmc_suspend = TRUE;
		ret = NOTIFY_OK;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		dhd_mmc_suspend = FALSE;
		ret = NOTIFY_OK;
		break;
	}
	smp_mb();
	return ret;
}

static struct notifier_block dhd_sleep_pm_notifier = {
	.notifier_call = dhd_sleep_pm_callback,
	.priority = 10
};
extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);
#endif 

static void dhd_set_packet_filter(int value, dhd_pub_t *dhd)
{
#ifdef PKT_FILTER_SUPPORT
	DHD_TRACE(("%s: %d\n", __FUNCTION__, value));
	
	
	if ((dhd_pkt_filter_enable && !dhd->dhcp_in_progress) &&
	    (!value || (dhd_check_ap_wfd_mode_set(dhd) == FALSE))) {
		int i;

		for (i = 0; i < dhd->pktfilter_count; i++) {
			dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
			dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
				value, dhd_master_mode);
		}
	}
#endif
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
void wl_android_set_screen_off(int off);
dhd_pub_t *pdhd = NULL;
#ifdef BCM4329_LOW_POWER
int LowPowerMode = 1;
extern char gatewaybuf[8+1]; 
char ip_str[32];
bool hasDLNA = false;
bool allowMulticast = false;
#endif
int dhd_set_keepalive(int value);
extern int wl_pattern_atoh(char *src, char *dst);
int is_screen_off = 0;
static int dhd_set_suspend(int value, dhd_pub_t *dhd)
{
	
#ifdef BCM4329_LOW_POWER
	int ignore_bcmc = 1;
#endif
	char iovbuf[32];

#ifdef CUSTOMER_HW2
#endif 
    char eventmask[WL_EVENTING_MASK_LEN];
    int ret = 0;
    is_screen_off = value;

	DHD_TRACE(("%s: enter, value = %d in_suspend=%d\n",
		__FUNCTION__, value, dhd->in_suspend));

	
	wl_android_set_screen_off(is_screen_off);
	

      
      if (is_screen_off) {
          bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
          if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
              DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
          }
          bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

          clrbit(eventmask, WLC_E_ACTION_FRAME_RX);
          clrbit(eventmask, WLC_E_ACTION_FRAME_COMPLETE);
          clrbit(eventmask, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE);
          clrbit(eventmask, WLC_E_P2P_PROBREQ_MSG);
          clrbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);

          
          bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
          if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
              DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
          }

      } else {
          bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
          if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
              DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
          }
          bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

          setbit(eventmask, WLC_E_ACTION_FRAME_RX);
          setbit(eventmask, WLC_E_ACTION_FRAME_COMPLETE);
          setbit(eventmask, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE);
          setbit(eventmask, WLC_E_P2P_PROBREQ_MSG);
          setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);

          
          bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
          if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
              DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
          }

      }

	if (dhd && dhd->up) {
		if (value && dhd->in_suspend) {
#ifdef PKT_FILTER_SUPPORT
				dhd->early_suspended = 1;
#endif
#ifdef BCM4329_LOW_POWER
		if (LowPowerMode == 1) {
			if (!hasDLNA && !allowMulticast) {
				
				printf("set ignore_bcmc = %d",ignore_bcmc);
				bcm_mkiovar("pm_ignore_bcmc", (char *)&ignore_bcmc,
					4, iovbuf, sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
				
				
			}
		}
#endif

				
				dhd_set_keepalive(1);

				
				DHD_TRACE(("%s: force extra Suspend setting \n", __FUNCTION__));

#ifdef PNO_SUPPORT
				
				dhd_set_pfn(dhd, 1);
#endif

				
				dhdhtc_set_power_control(0, DHDHTC_POWER_CTRL_BROWSER_LOAD_PAGE);
				dhdhtc_update_wifi_power_mode(is_screen_off);
				dhdhtc_update_dtim_listen_interval(is_screen_off);
			} else {
#ifdef PKT_FILTER_SUPPORT
				dhd->early_suspended = 0;
#endif
				dhdhtc_update_wifi_power_mode(is_screen_off);
				dhdhtc_update_dtim_listen_interval(is_screen_off);

				
				DHD_TRACE(("%s: Remove extra suspend setting \n", __FUNCTION__));

#ifdef PNO_SUPPORT
				dhd_set_pfn(dhd, 0);
#endif

#ifdef BCM4329_LOW_POWER
				if (LowPowerMode == 1) {
					ignore_bcmc = 0;
					
					printf("set ignore_bcmc = %d",ignore_bcmc);
					bcm_mkiovar("pm_ignore_bcmc", (char *)&ignore_bcmc,
						4, iovbuf, sizeof(iovbuf));
					dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
					
					
				}
				
				
				dhd_set_keepalive(0);
#endif
			}
	}

	return 0;
}
int dhd_set_keepalive(int value)
{
    char *str;
    int						str_len;
    int   buf_len;
    char buf[256];
    wl_keep_alive_pkt_t keep_alive_pkt;
    wl_keep_alive_pkt_t *keep_alive_pktp;
    dhd_pub_t *dhd = pdhd;
#ifdef HTC_KlocWork
	memset(&keep_alive_pkt, 0, sizeof(keep_alive_pkt));
#endif
    
    str = "keep_alive";
    str_len = strlen(str);
    strncpy(buf, str, str_len);
    buf[str_len] = '\0';
    buf_len = str_len + 1;
    keep_alive_pktp = (wl_keep_alive_pkt_t *) (buf + str_len + 1);

	keep_alive_pkt.period_msec = htod32(30000); 
	
	keep_alive_pkt.len_bytes = 0;
	buf_len += WL_KEEP_ALIVE_FIXED_LEN;
	bzero(keep_alive_pkt.data, sizeof(keep_alive_pkt.data));

    memcpy((char*)keep_alive_pktp, &keep_alive_pkt, WL_KEEP_ALIVE_FIXED_LEN);

    dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, buf_len, TRUE, 0);

    return 0;
}

static unsigned int dhdhtc_power_ctrl_mask = 0;
int dhdcdc_power_active_while_plugin = 1;
int dhdcdc_wifiLock = 0; 
extern int usb_get_connect_type(void); 
int dhdhtc_update_wifi_power_mode(int is_screen_off)
{
	int pm_type;
	dhd_pub_t *dhd = pdhd;

	if (!dhd) {
		printf("dhd is not attached\n");
		return -1;
	}

	if (dhdhtc_power_ctrl_mask) {
		printf("power active. ctrl_mask: 0x%x\n", dhdhtc_power_ctrl_mask);
		pm_type = PM_OFF;
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&pm_type, sizeof(pm_type), TRUE, 0);
	}  else if  (dhdcdc_power_active_while_plugin && usb_get_connect_type()) {
		printf("update pm: PM_FAST. usb_type:%d\n", usb_get_connect_type());
		pm_type = PM_FAST;
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&pm_type, sizeof(pm_type), TRUE, 0);
	} else {
		if (is_screen_off && !dhdcdc_wifiLock)
			pm_type = PM_MAX;
		else
			pm_type = PM_FAST;
		printf("update pm: %s, wifiLock: %d\n", pm_type==1?"PM_MAX":"PM_FAST", dhdcdc_wifiLock);
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&pm_type, sizeof(pm_type), TRUE, 0);
	}

	return 0;
}


int dhdhtc_set_power_control(int power_mode, unsigned int reason)
{

	if (reason < DHDHTC_POWER_CTRL_MAX_NUM) {
		if (power_mode) {
			dhdhtc_power_ctrl_mask |= 0x1<<reason;
		} else {
			dhdhtc_power_ctrl_mask &= ~(0x1<<reason);
		}


	} else {
		printf("%s: Error reason: %u", __func__, reason);
		return -1;
	}

	return 0;
}

unsigned int dhdhtc_get_cur_pwr_ctrl(void)
{
	return dhdhtc_power_ctrl_mask;
}

extern int wl_android_is_during_wifi_call(void);

int dhdhtc_update_dtim_listen_interval(int is_screen_off)
{
	char iovbuf[32];
	int bcn_li_dtim;
	int ret = 0;
	dhd_pub_t *dhd = pdhd;

	if (!dhd) {
		printf("dhd is not attached\n");
		return -1;
	}

	if (wl_android_is_during_wifi_call() || !is_screen_off || dhdcdc_wifiLock)
		bcn_li_dtim = 0;
	else
		bcn_li_dtim = 3;

	
	bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
		4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	printf("update dtim listern interval: %d\n", bcn_li_dtim);

	return ret;
}

static int dhd_suspend_resume_helper(struct dhd_info *dhd, int val, int force)
{
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_OS_WAKE_LOCK(dhdp);
	
	dhdp->in_suspend = val;
	if ((force || !dhdp->suspend_disable_flag) &&
		(dhd_check_ap_wfd_mode_set(dhdp) == FALSE))
	{
		ret = dhd_set_suspend(val, dhdp);
	}
	DHD_OS_WAKE_UNLOCK(dhdp);
	return ret;
}

static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 1, 0);
}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 0, 0);
}
#endif 


void
dhd_timeout_start(dhd_timeout_t *tmo, uint usec)
{
	tmo->limit = usec;
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = 1000000 / HZ;
}

int
dhd_timeout_expired(dhd_timeout_t *tmo)
{
	
	if (tmo->increment == 0) {
		tmo->increment = 1;
		return 0;
	}

	if (tmo->elapsed >= tmo->limit)
		return 1;

	
	tmo->elapsed += tmo->increment;

	if (tmo->increment < tmo->tick) {
		OSL_DELAY(tmo->increment);
		tmo->increment *= 2;
		if (tmo->increment > tmo->tick)
			tmo->increment = tmo->tick;
	} else {
		wait_queue_head_t delay_wait;
		DECLARE_WAITQUEUE(wait, current);
		int pending;
		init_waitqueue_head(&delay_wait);
		add_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		pending = signal_pending(current);
		remove_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (pending)
			return 1;	
	}

	return 0;
}

int
dhd_net2idx(dhd_info_t *dhd, struct net_device *net)
{
	int i = 0;

	ASSERT(dhd);
	while (i < DHD_MAX_IFS) {
		if (dhd->iflist[i] && (dhd->iflist[i]->net == net))
			return i;
		i++;
	}

	return DHD_BAD_IF;
}

struct net_device * dhd_idx2net(void *pub, int ifidx)
{
	struct dhd_pub *dhd_pub = (struct dhd_pub *)pub;
	struct dhd_info *dhd_info;

	if (!dhd_pub || ifidx < 0 || ifidx >= DHD_MAX_IFS)
		return NULL;
	dhd_info = dhd_pub->info;
	if (dhd_info && dhd_info->iflist[ifidx])
		return dhd_info->iflist[ifidx]->net;
	return NULL;
}

int
dhd_ifname2idx(dhd_info_t *dhd, char *name)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (dhd->iflist[i] && !strncmp(dhd->iflist[i]->name, name, IFNAMSIZ))
				break;

	DHD_TRACE(("%s: return idx %d for \"%s\"\n", __FUNCTION__, i, name));

	return i;	
}

char *
dhd_ifname(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	ASSERT(dhd);

	if (ifidx < 0 || ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: ifidx %d out of range\n", __FUNCTION__, ifidx));
		return "<if_bad>";
	}

	if (dhd->iflist[ifidx] == NULL) {
		DHD_ERROR(("%s: null i/f %d\n", __FUNCTION__, ifidx));
		return "<if_null>";
	}

	if (dhd->iflist[ifidx]->net)
		return dhd->iflist[ifidx]->net->name;

	return "<if_none>";
}

uint8 *
dhd_bssidx2bssid(dhd_pub_t *dhdp, int idx)
{
	int i;
	dhd_info_t *dhd = (dhd_info_t *)dhdp;

	ASSERT(dhd);
	for (i = 0; i < DHD_MAX_IFS; i++)
	if (dhd->iflist[i] && dhd->iflist[i]->bssidx == idx)
		return dhd->iflist[i]->mac_addr;

	return NULL;
}

void 
dhd_state_set_flags(dhd_pub_t *dhdp, dhd_attach_states_t flags, int add)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

        ASSERT(dhd);
	
	if (add) {
		DHD_INFO(("%s: add flags %x to dhd_state(%x).\n", __FUNCTION__, flags, dhd->dhd_state));
		dhd->dhd_state |= flags;
	} else {
		DHD_INFO(("%s: remove flags %x to dhd_state(%x).\n", __FUNCTION__, flags, dhd->dhd_state));
		dhd->dhd_state &= (~flags);
	}

	DHD_INFO(("%s: dhd_state=%x.\n", __FUNCTION__, dhd->dhd_state));
}

static void
_dhd_set_multicast_list(dhd_info_t *dhd, int ifidx)
{
	struct net_device *dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	struct netdev_hw_addr *ha;
#else
	struct dev_mc_list *mclist;
#endif
	uint32 allmulti, cnt;

	wl_ioctl_t ioc;
	char *buf, *bufp;
	uint buflen;
	int ret;

#ifdef MCAST_LIST_ACCUMULATION
	int i;
	uint32 cnt_iface[DHD_MAX_IFS];
	cnt = 0;
	allmulti = 0;

	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			dev = dhd->iflist[i]->net;
#else
			ASSERT(dhd && dhd->iflist[ifidx]);
			dev = dhd->iflist[ifidx]->net;
#endif 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_lock_bh(dev);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#ifdef MCAST_LIST_ACCUMULATION
			cnt_iface[i] = netdev_mc_count(dev);
			cnt += cnt_iface[i];
#else
			cnt = netdev_mc_count(dev);
#endif 
#else
#ifdef MCAST_LIST_ACCUMULATION
			cnt += dev->mc_count;
#else
			cnt = dev->mc_count;
#endif 
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_unlock_bh(dev);
#endif

			
#ifdef MCAST_LIST_ACCUMULATION
			allmulti |= (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;
		}
	}
#else
	allmulti = (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;
#endif 
#ifdef PASS_ALL_MCAST_PKTS
#ifdef PKT_FILTER_SUPPORT
	if (!dhd->pub.early_suspended)
#endif 
		allmulti = TRUE;
#endif 

	


	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	if (!(bufp = buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
		           dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strncpy(bufp, "mcast_list", sizeof("mcast_list"));
	bufp += sizeof("mcast_list");

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

#ifdef MCAST_LIST_ACCUMULATION
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			DHD_TRACE(("_dhd_set_multicast_list: ifidx %d\n", i));
			dev = dhd->iflist[i]->net;
#endif 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_lock_bh(dev);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
			netdev_for_each_mc_addr(ha, dev) {
#ifdef MCAST_LIST_ACCUMULATION
				if (!cnt_iface[i])
#else
				if (!cnt)
#endif 
					break;
				memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
				bufp += ETHER_ADDR_LEN;
#ifdef MCAST_LIST_ACCUMULATION
				DHD_TRACE(("_dhd_set_multicast_list: cnt "
					"%d %02x:%02x:%02x:%02x:%02x:%02x\n",
					cnt_iface[i],  ha->addr[0],  ha->addr[1],
					ha->addr[2],  ha->addr[3],  ha->addr[4],
					ha->addr[5]));
				cnt_iface[i]--;
#else
				cnt--;
#endif 
	}
#else
#ifdef MCAST_LIST_ACCUMULATION
			for (mclist = dev->mc_list; (mclist && (cnt_iface[i] > 0));
				cnt_iface[i]--, mclist = mclist->next) {
#else
			for (mclist = dev->mc_list; (mclist && (cnt > 0));
				cnt--, mclist = mclist->next) {
#endif 
				memcpy(bufp, (void *)mclist->dmi_addr, ETHER_ADDR_LEN);
				bufp += ETHER_ADDR_LEN;
			}
#endif 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_unlock_bh(dev);
#endif
#ifdef MCAST_LIST_ACCUMULATION
		}
	}
#endif 

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set mcast_list failed, cnt %d\n",
			dhd_ifname(&dhd->pub, ifidx), cnt));
		allmulti = cnt ? TRUE : allmulti;
	}

	MFREE(dhd->pub.osh, buf, buflen);


	buflen = sizeof("allmulti") + sizeof(allmulti);
	if (!(buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for allmulti\n", dhd_ifname(&dhd->pub, ifidx)));
		return;
	}
	allmulti = htol32(allmulti);

	if (!bcm_mkiovar("allmulti", (void*)&allmulti, sizeof(allmulti), buf, buflen)) {
		DHD_ERROR(("%s: mkiovar failed for allmulti, datalen %d buflen %u\n",
		           dhd_ifname(&dhd->pub, ifidx), (int)sizeof(allmulti), buflen));
		MFREE(dhd->pub.osh, buf, buflen);
		return;
	}


	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set allmulti %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}

	MFREE(dhd->pub.osh, buf, buflen);

	

#ifdef MCAST_LIST_ACCUMULATION
	allmulti = 0;
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			dev = dhd->iflist[i]->net;
			allmulti |= (dev->flags & IFF_PROMISC) ? TRUE : FALSE;
		}
	}
#else
	allmulti = (dev->flags & IFF_PROMISC) ? TRUE : FALSE;
#endif 
	allmulti = htol32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set promisc %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}
}

#ifdef CUSTOMER_HW4
int
#else
static int
#endif
_dhd_set_mac_address(dhd_info_t *dhd, int ifidx, struct ether_addr *addr)
{
	char buf[32];
	wl_ioctl_t ioc;
	int ret;

	if (module_remove) {
		printf("%s: module removed.\n", __FUNCTION__);
		return -1;
	}

	if (!bcm_mkiovar("cur_etheraddr", (char*)addr, ETHER_ADDR_LEN, buf, 32)) {
		DHD_ERROR(("%s: mkiovar failed for cur_etheraddr\n", dhd_ifname(&dhd->pub, ifidx)));
		return -1;
	}
	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = 32;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr failed\n", dhd_ifname(&dhd->pub, ifidx)));
	} else {
		memcpy(dhd->iflist[ifidx]->net->dev_addr, addr, ETHER_ADDR_LEN);
		memcpy(dhd->pub.mac.octet, addr, ETHER_ADDR_LEN);
	}

	return ret;
}

#ifdef SOFTAP
extern struct net_device *ap_net_dev;
extern tsk_ctl_t ap_eth_ctl; 
#endif

static void
dhd_op_if(dhd_if_t *ifp)
{
	dhd_info_t	*dhd;
	int ret = 0, err = 0;
#ifdef SOFTAP
	unsigned long flags;
#endif

	if (!ifp || !ifp->info || !ifp->idx)
		return;
	ASSERT(ifp && ifp->info && ifp->idx);	
	dhd = ifp->info;

	DHD_TRACE(("%s: idx %d, state %d\n", __FUNCTION__, ifp->idx, ifp->state));

#ifdef WL_CFG80211
	if (wl_cfg80211_is_progress_ifchange())
			return;

#endif
	switch (ifp->state) {
	case DHD_IF_ADD:
		if (ifp->net != NULL) {
			DHD_ERROR(("%s: ERROR: netdev:%s already exists, try free & unregister \n",
			 __FUNCTION__, ifp->net->name));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			free_netdev(ifp->net);
		}
		
		if (!(ifp->net = alloc_etherdev(sizeof(dhd)))) {
			DHD_ERROR(("%s: OOM - alloc_etherdev\n", __FUNCTION__));
			ret = -ENOMEM;
		}
		if (ret == 0) {
			strncpy(ifp->net->name, ifp->name, IFNAMSIZ);
			ifp->net->name[IFNAMSIZ - 1] = '\0';
			memcpy(netdev_priv(ifp->net), &dhd, sizeof(dhd));
#ifdef WL_CFG80211
			if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)
				if (!wl_cfg80211_notify_ifadd(ifp->net, ifp->idx, ifp->bssidx,
					(void*)dhd_net_attach)) {
					ifp->state = DHD_IF_NONE;
					ifp->event2cfg80211 = TRUE;
					return;
				}
#endif
			if ((err = dhd_net_attach(&dhd->pub, ifp->idx)) != 0) {
				DHD_ERROR(("%s: dhd_net_attach failed, err %d\n",
					__FUNCTION__, err));
				ret = -EOPNOTSUPP;
			} else {
#if defined(SOFTAP)
	#ifndef APSTA_CONCURRENT
		if (ap_fw_loaded && !(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
	#endif
				 
				flags = dhd_os_spin_lock(&dhd->pub);

				printf("%s save ptr to wl0.1 netdev for use in wl_iw.c \n",__FUNCTION__);
				
				ap_net_dev = ifp->net;
				 
				up(&ap_eth_ctl.sema);
				dhd_os_spin_unlock(&dhd->pub, flags);
	#ifndef APSTA_CONCURRENT
		}
	#endif
#endif
				DHD_TRACE(("\n ==== pid:%x, net_device for if:%s created ===\n\n",
					current->pid, ifp->net->name));
				ifp->state = DHD_IF_NONE;
			}
		}
		break;
	case DHD_IF_DEL:
		
		
		ifp->state = DHD_IF_DELETING;
		if (ifp->net != NULL) {
			DHD_TRACE(("\n%s: got 'DHD_IF_DEL' state\n", __FUNCTION__));
			netif_stop_queue(ifp->net);
#ifdef WL_CFG80211
			if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
				wl_cfg80211_ifdel_ops(ifp->net);
			}
#endif
			
			msleep(300);
			
			unregister_netdev(ifp->net);
			ret = DHD_DEL_IF;	
#ifdef WL_CFG80211
			if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
				wl_cfg80211_notify_ifdel();
			}
#endif
		}
		break;
	case DHD_IF_DELETING:
		break;
	default:
		DHD_ERROR(("%s: bad op %d\n", __FUNCTION__, ifp->state));
		ASSERT(!ifp->state);
		break;
	}

    if (ret < 0) {
        ifp->set_multicast = FALSE;
        if (ifp->net) {
#ifdef SOFTAP
            flags = dhd_os_spin_lock(&dhd->pub);
            if (ifp->net == ap_net_dev)
                ap_net_dev = NULL;   
            dhd_os_spin_unlock(&dhd->pub, flags);
#endif 
            free_netdev(ifp->net);
            ifp->net = NULL;
        }
        dhd->iflist[ifp->idx] = NULL;
        MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
    }
}

static int
_dhd_sysioc_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;


	int i;
#ifdef SOFTAP
	bool in_ap = FALSE;
	unsigned long flags;
#endif

#ifndef USE_KTHREAD_API
	DAEMONIZE("dhd_sysioc");

	complete(&tsk->completed);
#endif

	while (down_interruptible(&tsk->sema) == 0) {
#ifdef MCAST_LIST_ACCUMULATION
		bool set_multicast = FALSE;
#endif 

    	if (dhd->dhd_force_exit== TRUE)
	    		break;
		SMP_RD_BARRIER_DEPENDS();
		if (tsk->terminated) {
			break;
		}

		dhd_net_if_lock_local(dhd);
		DHD_OS_WAKE_LOCK(&dhd->pub);

		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				DHD_TRACE(("%s: interface %d\n", __FUNCTION__, i));
#ifdef SOFTAP
				flags = dhd_os_spin_lock(&dhd->pub);
				in_ap = (ap_net_dev != NULL);
				dhd_os_spin_unlock(&dhd->pub, flags);
#endif 
				if (dhd->iflist[i] && dhd->iflist[i]->state)
					dhd_op_if(dhd->iflist[i]);

				if (dhd->iflist[i] == NULL) {
					DHD_TRACE(("\n\n %s: interface %d just been removed,"
						"!\n\n", __FUNCTION__, i));
					continue;
				}
#ifdef SOFTAP
				if (in_ap && dhd->set_macaddress == i+1)  {
					DHD_TRACE(("attempt to set MAC for %s in AP Mode,"
						"blocked. \n", dhd->iflist[i]->net->name));
					dhd->set_macaddress = 0;
					continue;
				}

				if (in_ap && dhd->iflist[i]->set_multicast)  {
					DHD_TRACE(("attempt to set MULTICAST list for %s"
					 "in AP Mode, blocked. \n", dhd->iflist[i]->net->name));
					dhd->iflist[i]->set_multicast = FALSE;
					continue;
				}
#endif 
				if (dhd->pub.up == 0)
					continue;
				if (dhd->iflist[i]->set_multicast) {
					dhd->iflist[i]->set_multicast = FALSE;
#ifdef MCAST_LIST_ACCUMULATION
					set_multicast = TRUE;
#else
					_dhd_set_multicast_list(dhd, i);
#endif 
				}
				if (dhd->set_macaddress == i+1) {
					dhd->set_macaddress = 0;
					if (_dhd_set_mac_address(dhd, i, &dhd->macvalue) == 0) {
						DHD_INFO((
						"dhd_sysioc_thread: MACID is overwritten\n"));
					} else {
						DHD_ERROR((
					"dhd_sysioc_thread: _dhd_set_mac_address() failed\n"));
					}
				}
			}
		}
#ifdef MCAST_LIST_ACCUMULATION
		if (set_multicast)
			_dhd_set_multicast_list(dhd, 0);
#endif 

		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		dhd_net_if_unlock_local(dhd);
	}
	DHD_TRACE(("%s: stopped\n", __FUNCTION__));
	complete_and_exit(&tsk->completed, 0);
}

static int
dhd_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;

	printf("enter %s\n", __func__);

	
	if ( !dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)) {
		printf("%s: dhd is down. skip it.\n", __func__);
		return -ENODEV;
	}
	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return -1;

	ASSERT(dhd->thr_sysioc_ctl.thr_pid >= 0);
	memcpy(&dhd->macvalue, sa->sa_data, ETHER_ADDR_LEN);
	dhd->set_macaddress = ifidx+1;
	up(&dhd->thr_sysioc_ctl.sema);

	return ret;
}

static void
dhd_set_multicast_list(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ifidx;
	printf("enter %s\n", __func__);

	
	if ( !dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN) || module_remove) {
		printf("%s: dhd is down module_remove[%d]. skip it.\n", __func__,module_remove);
		return;
	}

	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return;

	ASSERT(dhd->thr_sysioc_ctl.thr_pid >= 0);
	dhd->iflist[ifidx]->set_multicast = TRUE;
	up(&dhd->thr_sysioc_ctl.sema);
}

#ifdef PROP_TXSTATUS
int
dhd_os_wlfc_block(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);
	ASSERT(di != NULL);
	spin_lock_bh(&di->wlfc_spinlock);
	return 1;
}

int
dhd_os_wlfc_unblock(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);

	(void)di;
	ASSERT(di != NULL);
	spin_unlock_bh(&di->wlfc_spinlock);
	return 1;
}

const uint8 wme_fifo2ac[] = { 0, 1, 2, 3, 1, 1 };
uint8 prio2fifo[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };
#define WME_PRIO2AC(prio)	wme_fifo2ac[prio2fifo[(prio)]]

#endif 
int
dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh = NULL;

	
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN)) {
		
		PKTFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}

	
	if (PKTLEN(dhdp->osh, pktbuf) >= ETHER_HDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
		eh = (struct ether_header *)pktdata;

		if (ETHER_ISMULTI(eh->ether_dhost))
			dhdp->tx_multicast++;
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X)
			atomic_inc(&dhd->pend_8021x_cnt);
	} else {
			PKTFREE(dhd->pub.osh, pktbuf, TRUE);
			return BCME_ERROR;
	}

	
	if (PKTPRIO(pktbuf) == 0)
		pktsetprio(pktbuf, FALSE);

#ifdef PROP_TXSTATUS
	if (dhdp->wlfc_state) {
		
		DHD_PKTTAG_SETIF(PKTTAG(pktbuf), ifidx);

		
		DHD_PKTTAG_SETDSTN(PKTTAG(pktbuf), eh->ether_dhost);

		
		if (ETHER_ISMULTI(eh->ether_dhost))
			
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), AC_COUNT);
		else
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), WME_PRIO2AC(PKTPRIO(pktbuf)));
	} else
#endif 
	
	dhd_prot_hdrpush(dhdp, ifidx, pktbuf);

	
#ifdef WLMEDIA_HTSF
	dhd_htsf_addtxts(dhdp, pktbuf);
#endif
#ifdef PROP_TXSTATUS
	if (dhdp->wlfc_state && ((athost_wl_status_info_t*)dhdp->wlfc_state)->proptxstatus_mode
		!= WLFC_FCMODE_NONE) {
		dhd_os_wlfc_block(dhdp);
		ret = dhd_wlfc_enque_sendq(dhdp->wlfc_state, DHD_PKTTAG_FIFO(PKTTAG(pktbuf)),
			pktbuf);
		dhd_wlfc_commit_packets(dhdp->wlfc_state,  (f_commitpkt_t)dhd_bus_txdata,
			dhdp->bus);
		if (((athost_wl_status_info_t*)dhdp->wlfc_state)->toggle_host_if) {
			((athost_wl_status_info_t*)dhdp->wlfc_state)->toggle_host_if = 0;
		}
		dhd_os_wlfc_unblock(dhdp);
	}
	else
		
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#else
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif 

	return ret;
}

static int txq_full_event_num = 0;
int
dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	void *pktbuf;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	int ifidx;
#ifdef WLMEDIA_HTSF
	uint8 htsfdlystat_sz = dhd->pub.htsfdlystat_sz;
#else
	uint8 htsfdlystat_sz = 0;
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (module_remove) {
		printf("%s: module removed.", __FUNCTION__);
		dev_kfree_skb(skb); 
		netif_stop_queue(net);
		return -ENODEV;
	}
	DHD_OS_WAKE_LOCK(&dhd->pub);

	
	if (dhd->pub.busstate == DHD_BUS_DOWN || dhd->pub.hang_was_sent) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d \n",
			__FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
		
		
		if (dhd->pub.up) {
			DHD_ERROR(("%s: Event HANG sent up\n", __FUNCTION__));
			net_os_send_hang_message(net);
		}
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __FUNCTION__, ifidx));
		netif_stop_queue(net);
		dev_kfree_skb(skb);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}

	

	if (skb_headroom(skb) < dhd->pub.hdrlen + htsfdlystat_sz) {
		struct sk_buff *skb2;

		DHD_INFO(("%s: insufficient headroom\n",
		          dhd_ifname(&dhd->pub, ifidx)));
		dhd->pub.tx_realloc++;

		skb2 = skb_realloc_headroom(skb, dhd->pub.hdrlen + htsfdlystat_sz);

		dev_kfree_skb(skb);
		if ((skb = skb2) == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
			           dhd_ifname(&dhd->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
	}

	
	if (!(pktbuf = PKTFRMNATIVE(dhd->pub.osh, skb))) {
		DHD_ERROR(("%s: PKTFRMNATIVE failed\n",
		           dhd_ifname(&dhd->pub, ifidx)));
		dev_kfree_skb_any(skb);
		ret = -ENOMEM;
		goto done;
	}
#ifdef WLMEDIA_HTSF
	if (htsfdlystat_sz && PKTLEN(dhd->pub.osh, pktbuf) >= ETHER_ADDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhd->pub.osh, pktbuf);
		struct ether_header *eh = (struct ether_header *)pktdata;

		if (!ETHER_ISMULTI(eh->ether_dhost) &&
			(ntoh16(eh->ether_type) == ETHER_TYPE_IP)) {
			eh->ether_type = hton16(ETHER_TYPE_BRCM_PKTDLYSTATS);
		}
	}
#endif

#if HTC_DUMP_PACKLEN
    if (PKTLEN(dhd->pub.osh, pktbuf) >= 1500) {
       printf("startx: send pkt len = %d\n", PKTLEN(dhd->pub.osh, pktbuf));
    }
#endif

	ret = dhd_sendpkt(&dhd->pub, ifidx, pktbuf);

	
	if ( ret == BCME_NORESOURCE ) {
		txq_full_event_num++;

		
		if ( txq_full_event_num >= MAX_TXQ_FULL_EVENT ) {
			txq_full_event_num = 0;
			net_os_send_hang_message(net);
		}
	}
	else {
		txq_full_event_num = 0;
	}

done:
	if (ret)
		dhd->pub.dstats.tx_dropped++;
	else
		dhd->pub.tx_packets++;

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
	return 0;
#else
	return NETDEV_TX_OK;
#endif
}

void
dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhdp->txoff = state;
	ASSERT(dhd);

	if (ifidx == ALL_INTERFACES) {
		
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				net = dhd->iflist[i]->net;
				if (state == ON)
					netif_stop_queue(net);
				else
					netif_wake_queue(net);
			}
		}
	}
	else {
		if (dhd->iflist[ifidx]) {
			net = dhd->iflist[ifidx]->net;
			if (state == ON)
				netif_stop_queue(net);
			else
				netif_wake_queue(net);
		}
	}
}

#ifdef DHD_RX_DUMP
typedef struct {
	uint16 type;
	const char *str;
} PKTTYPE_INFO;

static const PKTTYPE_INFO packet_type_info[] =
{
	{ ETHER_TYPE_IP, "IP" },
	{ ETHER_TYPE_ARP, "ARP" },
	{ ETHER_TYPE_BRCM, "BRCM" },
	{ ETHER_TYPE_802_1X, "802.1X" },
	{ ETHER_TYPE_WAI, "WAPI" },
	{ 0, ""}
};

static const char *_get_packet_type_str(uint16 type)
{
	int i;
	int n = sizeof(packet_type_info)/sizeof(packet_type_info[1]) - 1;

	for (i = 0; i < n; i++) {
		if (packet_type_info[i].type == type)
			return packet_type_info[i].str;
	}

	return packet_type_info[n].str;
}
#endif 

void
dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *pktbuf, int numpkt, uint8 chan)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	uchar *eth;
	uint len;
	void *data = NULL, *pnext = NULL;
	int i;
	dhd_if_t *ifp;
	wl_event_msg_t event;
	int tout_rx = 0;
	int tout_ctrl = 0;
#ifdef DHD_RX_DUMP
#ifdef DHD_RX_FULL_DUMP
	int k;
#endif 
	char *dump_data;
	uint16 protocol;
#endif 
#ifdef HTC_KlocWork
	memset(&event,0,sizeof(event));
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (module_remove || (!module_insert)) {
		for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
			pnext = PKTNEXT(dhdp->osh, pktbuf);
			PKTSETNEXT(wl->sh.osh, pktbuf, NULL);
			skb = PKTTONATIVE(dhdp->osh, pktbuf);
			dev_kfree_skb_any(skb);
		}
		if (!module_insert)
			DHD_ERROR(("%s: module not insert, skip\n", __FUNCTION__));
		else
			DHD_ERROR(("%s: module removed. skip rx frame\n", __FUNCTION__));
		return;
	}

	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
#ifdef WLBTAMP
		struct ether_header *eh;
		struct dot11_llc_snap_header *lsh;
#endif

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL) {
			DHD_ERROR(("%s: ifp is NULL. drop packet\n",
				__FUNCTION__));
			PKTFREE(dhdp->osh, pktbuf, TRUE);
			continue;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		
		if (!ifp->net || ifp->net->reg_state != NETREG_REGISTERED ||
			!dhd->pub.up) {
			DHD_ERROR(("%s: net device is NOT registered yet. drop packet\n",
			__FUNCTION__));
			PKTFREE(dhdp->osh, pktbuf, TRUE);
			continue;
		}
#endif

		pnext = PKTNEXT(dhdp->osh, pktbuf);
		PKTSETNEXT(wl->sh.osh, pktbuf, NULL);

#ifdef WLBTAMP
		eh = (struct ether_header *)PKTDATA(wl->sh.osh, pktbuf);
		lsh = (struct dot11_llc_snap_header *)&eh[1];

		if ((ntoh16(eh->ether_type) < ETHER_TYPE_MIN) &&
		    (PKTLEN(wl->sh.osh, pktbuf) >= RFC1042_HDR_LEN) &&
		    bcmp(lsh, BT_SIG_SNAP_MPROT, DOT11_LLC_SNAP_HDR_LEN - 2) == 0 &&
		    lsh->type == HTON16(BTA_PROT_L2CAP)) {
			amp_hci_ACL_data_t *ACL_data = (amp_hci_ACL_data_t *)
			        ((uint8 *)eh + RFC1042_HDR_LEN);
			ACL_data = NULL;
		}
#endif 

#ifdef PROP_TXSTATUS
		if (dhdp->wlfc_state && PKTLEN(wl->sh.osh, pktbuf) == 0) {
			((athost_wl_status_info_t*)dhdp->wlfc_state)->stats.wlfc_header_only_pkt++;
			PKTFREE(dhdp->osh, pktbuf, TRUE);
			continue;
		}
#endif

		skb = PKTTONATIVE(dhdp->osh, pktbuf);

		eth = skb->data;
		len = skb->len;

#ifdef DHD_RX_DUMP
		dump_data = skb->data;
		protocol = (dump_data[12] << 8) | dump_data[13];
		DHD_ERROR(("RX DUMP - %s\n", _get_packet_type_str(protocol)));

#ifdef DHD_RX_FULL_DUMP
		if (protocol != ETHER_TYPE_BRCM) {
			for (k = 0; k < skb->len; k++) {
				DHD_ERROR(("%02X ", dump_data[k]));
				if ((k & 15) == 15)
					DHD_ERROR(("\n"));
			}
			DHD_ERROR(("\n"));
		}
#endif 

		if (protocol != ETHER_TYPE_BRCM) {
			if (dump_data[0] == 0xFF) {
				DHD_ERROR(("%s: BROADCAST\n", __FUNCTION__));

				if ((dump_data[12] == 8) &&
					(dump_data[13] == 6)) {
					DHD_ERROR(("%s: ARP %d\n",
						__FUNCTION__, dump_data[0x15]));
				}
			} else if (dump_data[0] & 1) {
				DHD_ERROR(("%s: MULTICAST: "
					"%02X:%02X:%02X:%02X:%02X:%02X\n",
					__FUNCTION__, dump_data[0],
					dump_data[1], dump_data[2],
					dump_data[3], dump_data[4],
					dump_data[5]));
			}

			if (protocol == ETHER_TYPE_802_1X) {
				DHD_ERROR(("ETHER_TYPE_802_1X: "
					"ver %d, type %d, replay %d\n",
					dump_data[14], dump_data[15],
					dump_data[30]));
			}
		}

#endif 

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL)
			ifp = dhd->iflist[0];

		ASSERT(ifp);
		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST) {
			dhd->pub.rx_multicast++;
		}

		skb->data = eth;
		skb->len = len;

#ifdef WLMEDIA_HTSF
		dhd_htsf_addrxts(dhdp, pktbuf);
#endif
		
		skb_pull(skb, ETH_HLEN);
#ifdef BRCM_WPSAP
		
		if (ntoh16(skb->protocol) == ETHER_TYPE_802_1X){
#if 0
			int plen = 0;
			printk("@@@ got eap packet start! \n");
			for(plen = 0; plen<len ; plen++){
				printk("%02x ",eth[plen]);
				if((plen + 1 )%8 == 0)
					printk("\n");
			}
			printk("\n");
			printk("@@@ got eap packet End! \n");
#endif
			if(eth[22] == 0x01) {
				
				ASSERT(dhd->iflist[ifidx]->net != NULL);

				if (dhd->iflist[ifidx]->net)
					wl_iw_send_priv_event(dhd->iflist[ifidx]->net, "WPS_START");
			}
		}
#endif 

		
		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM) {
			dhd_wl_host_event(dhd, &ifidx,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
			skb->mac_header,
#else
			skb->mac.raw,
#endif
			&event,
			&data);


			wl_event_to_host_order(&event);

			if (!tout_ctrl)
				tout_ctrl = DHD_PACKET_TIMEOUT_MS;

#ifdef WLBTAMP
			if (event.event_type == WLC_E_BTA_HCI_EVENT) {
#ifdef HTC_KlocWork
				if(!data) {
					printf("[HTCKW] dhd_rx_frame: data=NULL\n");
				}
				else
#endif
				dhd_bta_doevt(dhdp, data, event.datalen);
			}
			tout = DHD_EVENT_TIMEOUT_MS;
#endif 

#if defined(PNO_SUPPORT)
			if (event.event_type == WLC_E_PFN_NET_FOUND) {
				tout_ctrl *= 2;
			}
#endif 
		} else {
			tout_rx = DHD_PACKET_TIMEOUT_MS;
		}

		ASSERT(ifidx < DHD_MAX_IFS && dhd->iflist[ifidx]);
		if (dhd->iflist[ifidx] && !dhd->iflist[ifidx]->state)
			ifp = dhd->iflist[ifidx];

		if (ifp->net)
			ifp->net->last_rx = jiffies;

		dhdp->dstats.rx_bytes += skb->len;
		dhdp->rx_packets++; 

		if (in_interrupt()) {
			netif_rx(skb);
		} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
			netif_rx_ni(skb);
#else
			ulong flags;
			netif_rx(skb);
			local_irq_save(flags);
			RAISE_RX_SOFTIRQ();
			local_irq_restore(flags);
#endif 
		}
	}
	DHD_OS_WAKE_LOCK_RX_TIMEOUT_ENABLE(dhdp, tout_rx);
	DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhdp, tout_ctrl);
}

void
dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx)
{
	
	return;
}

void
dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success)
{
	uint ifidx;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh;
	uint16 type;
#ifdef WLBTAMP
	uint len;
#endif

	dhd_prot_hdrpull(dhdp, &ifidx, txp, NULL, NULL);

	eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
	type  = ntoh16(eh->ether_type);

	if (type == ETHER_TYPE_802_1X)
		atomic_dec(&dhd->pend_8021x_cnt);

#ifdef WLBTAMP
	len = PKTLEN(dhdp->osh, txp);

	
	if ((type < ETHER_TYPE_MIN) && (len >= RFC1042_HDR_LEN)) {
		struct dot11_llc_snap_header *lsh = (struct dot11_llc_snap_header *)&eh[1];

		if (bcmp(lsh, BT_SIG_SNAP_MPROT, DOT11_LLC_SNAP_HDR_LEN - 2) == 0 &&
		    ntoh16(lsh->type) == BTA_PROT_L2CAP) {

			dhd_bta_tx_hcidata_complete(dhdp, txp, success);
		}
	}
#endif 
}

static struct net_device_stats *
dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_if_t *ifp;
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD_IF\n", __FUNCTION__));
		return NULL;
	}

	ifp = dhd->iflist[ifidx];
	ASSERT(dhd && ifp);

	if (dhd->pub.up) {
		if (module_remove) {
			printf("%s: module removed. return old value. ifp=%p, dhd=%p\n", __FUNCTION__, ifp, dhd);
		} else 
			
			dhd_prot_dstats(&dhd->pub);
	}

	
	ifp->stats.rx_packets = dhd->pub.dstats.rx_packets;
	ifp->stats.tx_packets = dhd->pub.dstats.tx_packets;
	ifp->stats.rx_bytes = dhd->pub.dstats.rx_bytes;
	ifp->stats.tx_bytes = dhd->pub.dstats.tx_bytes;
	ifp->stats.rx_errors = dhd->pub.dstats.rx_errors;
	ifp->stats.tx_errors = dhd->pub.dstats.tx_errors;
	ifp->stats.rx_dropped = dhd->pub.dstats.rx_dropped;
	ifp->stats.tx_dropped = dhd->pub.dstats.tx_dropped;
	ifp->stats.multicast = dhd->pub.dstats.multicast;

	return &ifp->stats;
}

#ifdef DHDTHREAD
static int
dhd_watchdog_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
	if (dhd_watchdog_prio > 0) {
		struct sched_param param;
		param.sched_priority = (dhd_watchdog_prio < MAX_RT_PRIO)?
			dhd_watchdog_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

#ifndef USE_KTHREAD_API
	DAEMONIZE("dhd_watchdog");

	
	complete(&tsk->completed);
#endif

	while (1)
		if (down_interruptible (&tsk->sema) == 0) {
			unsigned long flags;
			if (dhd->dhd_force_exit== TRUE)
				break;

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			dhd_os_sdlock(&dhd->pub);
			if (dhd->pub.dongle_reset == FALSE) {
				DHD_TIMER(("%s:\n", __FUNCTION__));

				
				dhd_bus_watchdog(&dhd->pub);

				flags = dhd_os_spin_lock(&dhd->pub);
				
				dhd->pub.tickcnt++;
				
				if (dhd->wd_timer_valid)
					mod_timer(&dhd->timer,
					jiffies + dhd_watchdog_ms * HZ / 1000);
				dhd_os_spin_unlock(&dhd->pub, flags);
			}
			dhd_os_sdunlock(&dhd->pub);
			DHD_OS_WAKE_UNLOCK(&dhd->pub);
		} else {
			break;
	}

	complete_and_exit(&tsk->completed, 0);
}
#endif 

static void dhd_watchdog(ulong data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;
	unsigned long flags;

	DHD_OS_WAKE_LOCK(&dhd->pub);
	if (dhd->pub.dongle_reset) {
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return;
	}

#ifdef DHDTHREAD
	if (dhd->thr_wdt_ctl.thr_pid >= 0) {
		up(&dhd->thr_wdt_ctl.sema);
		return;
	} else {
		DHD_ERROR(("watch_dog thr stopped, ignore\n"));
		return;
	}
#endif 

	dhd_os_sdlock(&dhd->pub);
	
	dhd_bus_watchdog(&dhd->pub);

	flags = dhd_os_spin_lock(&dhd->pub);
	
	dhd->pub.tickcnt++;

	
	if (dhd->wd_timer_valid)
		mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);
	dhd_os_spin_unlock(&dhd->pub, flags);
	dhd_os_sdunlock(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
}

int wlan_ioprio_idle = 0;
static int prev_wlan_ioprio_idle=0;
static inline void set_wlan_ioprio(void)
{
        int ret, prio;

        if(wlan_ioprio_idle == 1){
                prio = ((IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT) | 0);
        } else {
                prio = ((IOPRIO_CLASS_NONE << IOPRIO_CLASS_SHIFT) | 4);
        }
        ret = set_task_ioprio(current, prio);
        DHD_DEFAULT(("set_wlan_ioprio: prio=0x%X, ret=%d\n", prio, ret));
}

#ifdef DHDTHREAD
static int
dhd_dpc_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;

	if (dhd_dpc_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_dpc_prio < MAX_RT_PRIO)?dhd_dpc_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

#ifndef USE_KTHREAD_API
	DAEMONIZE("dhd_dpc");
	

	
	complete(&tsk->completed);
#endif

	
	while (1) {
        
        if(prev_wlan_ioprio_idle != wlan_ioprio_idle){
            set_wlan_ioprio();
            prev_wlan_ioprio_idle = wlan_ioprio_idle;
        }
        
		if (down_interruptible(&tsk->sema) == 0) {
			if (dhd->dhd_force_exit== TRUE)
				break;

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				if (dhd_bus_dpc(dhd->pub.bus)) {
					up(&tsk->sema);
				}
				else {
					DHD_OS_WAKE_UNLOCK(&dhd->pub);
				}
			} else {
				if (dhd->pub.up)
					dhd_bus_stop(dhd->pub.bus, TRUE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
			}
		}
		else
			break;
	}

	complete_and_exit(&tsk->completed, 0);
}
#endif 

static void
dhd_dpc(ulong data)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)data;

	
	if (dhd->pub.busstate != DHD_BUS_DOWN) {
		if (dhd_bus_dpc(dhd->pub.bus))
			tasklet_schedule(&dhd->tasklet);
		else
			DHD_OS_WAKE_UNLOCK(&dhd->pub);
	} else {
		dhd_bus_stop(dhd->pub.bus, TRUE);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
	}
}

void
dhd_sched_dpc(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	DHD_OS_WAKE_LOCK(dhdp);
#ifdef DHDTHREAD
	if (dhd->thr_dpc_ctl.thr_pid >= 0) {
		up(&dhd->thr_dpc_ctl.sema);
		return;
	}
#endif 

	if (dhd->dhd_tasklet_create)
		tasklet_schedule(&dhd->tasklet);
}

#ifdef TOE
static int
dhd_toe_get(dhd_info_t *dhd, int ifidx, uint32 *toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = FALSE;

	strncpy(buf, "toe_ol", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		
		if (ret == -EIO) {
			DHD_ERROR(("%s: toe not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}

		DHD_INFO(("%s: could not get toe_ol: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	memcpy(toe_ol, buf, sizeof(uint32));
	return 0;
}

static int
dhd_toe_set(dhd_info_t *dhd, int ifidx, uint32 toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int toe, ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = TRUE;

	

	strncpy(buf, "toe_ol", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	memcpy(&buf[sizeof("toe_ol")], &toe_ol, sizeof(uint32));

	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe_ol: ret=%d\n",
			dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	

	toe = (toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe, sizeof(uint32));

	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	return 0;
}
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void
dhd_ethtool_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);

	snprintf(info->driver, sizeof(info->driver), "wl");
	snprintf(info->version, sizeof(info->version), "%lu", dhd->pub.drv_version);
}

struct ethtool_ops dhd_ethtool_ops = {
	.get_drvinfo = dhd_ethtool_get_drvinfo
};
#endif 


#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
static int
dhd_ethtool(dhd_info_t *dhd, void *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	uint32 cmd;
#ifdef TOE
	struct ethtool_value edata;
	uint32 toe_cmpnt, csum_dir;
	int ret;
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	if (copy_from_user(&cmd, uaddr, sizeof (uint32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(info.driver));
		drvname[sizeof(info.driver)-1] = '\0';

		
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		
		if (strcmp(drvname, "?dhd") == 0) {
			snprintf(info.driver, sizeof(info.driver), "dhd");
			strncpy(info.version, EPI_VERSION_STR, sizeof(info.version) - 1);
			info.version[sizeof(info.version) - 1] = '\0';
		}

		
		else if (!dhd->pub.up) {
			DHD_ERROR(("%s: dongle is not up\n", __FUNCTION__));
			return -ENODEV;
		}

		
		else if (dhd->pub.iswl)
			snprintf(info.driver, sizeof(info.driver), "wl");
		else
			snprintf(info.driver, sizeof(info.driver), "xx");

		snprintf(info.version, sizeof(info.version), "%lu", dhd->pub.drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		DHD_CTL(("%s: given %*s, returning %s\n", __FUNCTION__,
		         (int)sizeof(drvname), drvname, info.driver));
		break;

#ifdef TOE
	
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_GTXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		edata.cmd = cmd;
		edata.data = (toe_cmpnt & csum_dir) ? 1 : 0;

		if (copy_to_user(uaddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;

	
	case ETHTOOL_SRXCSUM:
	case ETHTOOL_STXCSUM:
		if (copy_from_user(&edata, uaddr, sizeof(edata)))
			return -EFAULT;

		
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		if ((ret = dhd_toe_set(dhd, 0, toe_cmpnt)) < 0)
			return ret;

		
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				dhd->iflist[0]->net->features |= NETIF_F_IP_CSUM;
			else
				dhd->iflist[0]->net->features &= ~NETIF_F_IP_CSUM;
		}

		break;
#endif 

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
#endif 

static bool dhd_check_hang(struct net_device *net, dhd_pub_t *dhdp, int error)
{
	dhd_info_t * dhd;
	if (!dhdp)
		return FALSE;
	dhd = (dhd_info_t *)dhdp->info;
	if (dhd->thr_sysioc_ctl.thr_pid < 0) {
		DHD_ERROR(("%s : skipped due to negative pid - unloading?\n", __FUNCTION__));
		return FALSE;
	}

	if ((error == -ETIMEDOUT) || (error == -EREMOTEIO) ||
		((dhdp->busstate == DHD_BUS_DOWN) && (!dhdp->dongle_reset))) {
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d e=%d s=%d\n", __FUNCTION__,
			dhdp->rxcnt_timeout, dhdp->txcnt_timeout, error, dhdp->busstate));
		net_os_send_hang_message(net);
		return TRUE;
	}
	return FALSE;
}

static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int buflen = 0;
	void *buf = NULL;
	uint driver = 0;
	int ifidx;
	int ret;

	if (module_remove) {
		printf("%s: module removed. cmd 0x%04x\n", __FUNCTION__, cmd);
		return -1;
	}

	
	if ( !dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)){
		printf("%s: dhd is down. skip it.\n", __func__);
		return -ENODEV;
	}
	DHD_OS_WAKE_LOCK(&dhd->pub);

	
	if (dhd->pub.hang_was_sent) {
		DHD_ERROR(("%s: HANG was sent up earlier\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(&dhd->pub, DHD_EVENT_TIMEOUT_MS);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return OSL_ERROR(BCME_DONGLE_DOWN);
	}

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD IF\n", __FUNCTION__));
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}

#if defined(CONFIG_WIRELESS_EXT)
	
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		
		ret = wl_iw_ioctl(net, ifr, cmd);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif 

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif 

	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(net, ifr, cmd);
		dhd_check_hang(net, &dhd->pub, ret);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

	if (cmd != SIOCDEVPRIVATE) {
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

	
	if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
		bcmerror = -BCME_BADADDR;
		goto done;
	}

	
	if (ioc.buf) {
		if (ioc.len == 0) {
			DHD_TRACE(("%s: ioc.len=0, returns BCME_BADARG \n", __FUNCTION__));
			bcmerror = -BCME_BADARG;
			goto done;
		}
		buflen = MIN(ioc.len, DHD_IOCTL_MAXLEN);
		
		{
			if (!(buf = (char*)MALLOC(dhd->pub.osh, buflen))) {
				bcmerror = -BCME_NOMEM;
				goto done;
			}
			if (copy_from_user(buf, ioc.buf, buflen)) {
				bcmerror = -BCME_BADADDR;
				goto done;
			}
		}
	}

	
	if ((copy_from_user(&driver, (char *)ifr->ifr_data + sizeof(wl_ioctl_t),
		sizeof(uint)) != 0)) {
		bcmerror = -BCME_BADADDR;
		goto done;
	}

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = -BCME_EPERM;
		goto done;
	}

	
	if (driver == DHD_IOCTL_MAGIC) {
		bcmerror = dhd_ioctl((void *)&dhd->pub, &ioc, buf, buflen);
		if (bcmerror)
			dhd->pub.bcmerror = bcmerror;
		goto done;
	}

	
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	if (!dhd->pub.iswl) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	if (ioc.cmd == WLC_SET_KEY ||
	    (ioc.cmd == WLC_SET_VAR && ioc.buf != NULL &&
	     strncmp("wsec_key", ioc.buf, 9) == 0) ||
	    (ioc.cmd == WLC_SET_VAR && ioc.buf != NULL &&
	     strncmp("bsscfg:wsec_key", ioc.buf, 15) == 0) ||
	    ioc.cmd == WLC_DISASSOC)
		dhd_wait_pend8021x(net);

#ifdef WLMEDIA_HTSF
	if (ioc.buf) {
		
		if (strcmp("htsf", ioc.buf) == 0) {
			dhd_ioctl_htsf_get(dhd, 0);
			return BCME_OK;
		}

		if (strcmp("htsflate", ioc.buf) == 0) {
			if (ioc.set) {
				memset(ts, 0, sizeof(tstamp_t)*TSMAX);
				memset(&maxdelayts, 0, sizeof(tstamp_t));
				maxdelay = 0;
				tspktcnt = 0;
				maxdelaypktno = 0;
				memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			} else {
				dhd_dump_latency();
			}
			return BCME_OK;
		}
		if (strcmp("htsfclear", ioc.buf) == 0) {
			memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			htsf_seqnum = 0;
			return BCME_OK;
		}
		if (strcmp("htsfhis", ioc.buf) == 0) {
			dhd_dump_htsfhisto(&vi_d1, "H to D");
			dhd_dump_htsfhisto(&vi_d2, "D to D");
			dhd_dump_htsfhisto(&vi_d3, "D to H");
			dhd_dump_htsfhisto(&vi_d4, "H to H");
			return BCME_OK;
		}
		if (strcmp("tsport", ioc.buf) == 0) {
			if (ioc.set) {
				memcpy(&tsport, ioc.buf + 7, 4);
			} else {
				DHD_ERROR(("current timestamp port: %d \n", tsport));
			}
			return BCME_OK;
		}
	}
#endif 

	if ((ioc.cmd == WLC_SET_VAR || ioc.cmd == WLC_GET_VAR) &&
		ioc.buf != NULL && strncmp("rpc_", ioc.buf, 4) == 0) {
#ifdef BCM_FD_AGGR
		bcmerror = dhd_fdaggr_ioctl(&dhd->pub, ifidx, (wl_ioctl_t *)&ioc, buf, buflen);

#else
		bcmerror = BCME_UNSUPPORTED;
#endif
		goto done;
	}
	bcmerror = dhd_wl_ioctl(&dhd->pub, ifidx, (wl_ioctl_t *)&ioc, buf, buflen);

done:
	dhd_check_hang(net, &dhd->pub, bcmerror);

	if (!bcmerror && buf && ioc.buf) {
		if (copy_to_user(ioc.buf, buf, buflen))
			bcmerror = -EFAULT;
	}

	if (buf)
		MFREE(dhd->pub.osh, buf, buflen);

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return OSL_ERROR(bcmerror);
}

#ifdef WL_CFG80211
static int
dhd_cleanup_virt_ifaces(dhd_info_t *dhd)
{
	int i = 1; 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	int rollback_lock = FALSE;
#endif

	DHD_TRACE(("%s: Enter \n", __func__));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	
	if (rtnl_is_locked()) {
		rtnl_unlock();
		rollback_lock = TRUE;
	}
#endif

	for (i = 1; i < DHD_MAX_IFS; i++) {
		dhd_net_if_lock_local(dhd);
		if (dhd->iflist[i]) {
			DHD_TRACE(("Deleting IF: %d \n", i));
			if ((dhd->iflist[i]->state != DHD_IF_DEL) &&
				(dhd->iflist[i]->state != DHD_IF_DELETING)) {
				dhd->iflist[i]->state = DHD_IF_DEL;
				dhd->iflist[i]->idx = i;
				dhd_op_if(dhd->iflist[i]);
			}
		}
		dhd_net_if_unlock_local(dhd);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if (rollback_lock)
		rtnl_lock();
#endif

	return 0;
}
#endif 

#if defined(WL_CFG80211) && defined(SUPPORT_DEEP_SLEEP)
int sleep_never = 0;
#endif
static int
dhd_stop(struct net_device *net)
{
	int ifidx = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_TRACE(("%s: Enter %p\n", __FUNCTION__, net));
	if (dhd->pub.up == 0) {
		goto exit;
	}
	ifidx = dhd_net2idx(dhd, net);
	BCM_REFERENCE(ifidx);

	module_remove = 1;
#if defined(HW_OOB)
    bcmsdh_oob_intr_set(FALSE);
#endif

#ifdef WL_CFG80211
	if (ifidx == 0) {
		wl_cfg80211_down(NULL);

#ifndef APSTA_CONCURRENT
		if ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
			(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
			dhd_cleanup_virt_ifaces(dhd);
		}
#else
		if (ap_net_dev || ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
			(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211))) {
			printf("clean interface and free parameters");
			
			dhd_cleanup_virt_ifaces(dhd);
			ap_net_dev = NULL;
			ap_cfg_running = FALSE;
		}
	}
#endif
#endif

#ifdef PROP_TXSTATUS
	dhd_wlfc_cleanup(&dhd->pub);
#endif
	
	dhd->pub.up = 0;
	netif_stop_queue(net);

	
	dhd_prot_stop(&dhd->pub);

#if defined(WL_CFG80211)
	if (ifidx == 0 && !dhd_download_fw_on_driverload)
		wl_android_wifi_off(net);
#endif 
	dhd->pub.hang_was_sent = 0;
	dhd->pub.rxcnt_timeout = 0;
	dhd->pub.txcnt_timeout = 0;
	OLD_MOD_DEC_USE_COUNT;
exit:
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	return 0;
}

#define BCM4330B1_STA_FW_PATH "/system/etc/firmware/fw_bcm4330_b1.bin"
#define BCM4330B1_APSTA_FW_PATH "/system/etc/firmware/fw_bcm4330_apsta_b1.bin"
#define BCM4330B1_P2P_FW_PATH "/system/etc/firmware/fw_bcm4330_p2p_b1.bin"
#define BCM4330B1_MFG_FW_PATH "/system/etc/firmware/bcm_mfg.bin"
#define BCM4330B2_STA_FW_PATH "/system/etc/firmware/fw_bcm4330_b2.bin"
#define BCM4330B2_APSTA_FW_PATH "/system/etc/firmware/fw_bcm4330_apsta_b2.bin"
#define BCM4330B2_P2P_FW_PATH "/system/etc/firmware/fw_bcm4330_p2p_b2.bin"
#define BCM4330B2_MFG_FW_PATH "/system/etc/firmware/bcm_mfg2.bin"

static int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	uint up = 0;
#ifdef TOE
	uint32 toe_ol;
#endif
	int ifidx;
	int32 ret = 0;
	int32 dhd_open_retry_count = 0;
dhd_open_retry:
	DHD_OS_WAKE_LOCK(&dhd->pub);

	module_remove = 0;

	if (module_remove) {
		printf("%s: module removed. Just return.\n", __FUNCTION__);
		return -1;
	}

	
	printf(" firmware_path = %s\n", firmware_path);
	if (strlen(firmware_path) != 0) {
		if (bcm_chip_is_4330) {
			printf("chip is 4330!\n");
			if (strstr(firmware_path, "_apsta") != NULL) {
				if (bcm_chip_is_4330b1)
					strcpy(fw_path, BCM4330B1_APSTA_FW_PATH);
				else
					strcpy(fw_path, BCM4330B2_APSTA_FW_PATH);
			} else if (strstr(firmware_path, "_p2p") != NULL) {
				if (bcm_chip_is_4330b1)
					strcpy(fw_path, BCM4330B1_P2P_FW_PATH);
				else
					strcpy(fw_path, BCM4330B2_P2P_FW_PATH);
			} else if (strstr(firmware_path, "mfg") != NULL) {
				if (bcm_chip_is_4330b1)
					strcpy(fw_path, BCM4330B1_MFG_FW_PATH);
				else
					strcpy(fw_path, BCM4330B2_MFG_FW_PATH);
			} else {
				if (bcm_chip_is_4330b1)
					strcpy(fw_path, BCM4330B1_STA_FW_PATH);
				else
					strcpy(fw_path, BCM4330B2_STA_FW_PATH);
			}
		} else {
			if (firmware_path[strlen(firmware_path)-1] == '\n')
				firmware_path[strlen(firmware_path)-1] = '\0';
			strncpy(fw_path, firmware_path, sizeof(fw_path)-1);
			fw_path[sizeof(fw_path)-1] = '\0';
#if defined(SUPPORT_MULTIPLE_REVISION)
			ret = concate_revision(dhd->pub.bus, fw_path, MOD_PARAM_PATHLEN);
			if (ret != 0) {
				DHD_ERROR(("%s: fail to concatnate revison \n", __FUNCTION__));
				goto exit;
			}
#endif
		}
		firmware_path[0] = '\0';
	}

	dhd->pub.dongle_trap_occured = 0;
	dhd->pub.hang_was_sent = 0;
#if !defined(WL_CFG80211)
	ret = wl_control_wl_start(net);
	if (ret != 0) {
		DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
		ret = -1;
		goto exit;
	}
#endif 

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

#ifdef HTC_KlocWork
	if (ifidx < 0) {
		DHD_ERROR(("%s: Error: called with invalid IF\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}
#endif

	if (!dhd->iflist[ifidx] || dhd->iflist[ifidx]->state == DHD_IF_DEL) {
		DHD_ERROR(("%s: Error: called when IF already deleted\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (ifidx == 0) {
		atomic_set(&dhd->pend_8021x_cnt, 0);
#if defined(WL_CFG80211)
		DHD_ERROR(("\n%s\n", dhd_version));
		if (!dhd_download_fw_on_driverload) {
			ret = wl_android_wifi_on(net);
			if (ret != 0) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				ret = -1;
				goto exit;
			}
		} else {
#ifdef SUPPORT_DEEP_SLEEP
			if (sleep_never) {
				dhd_deepsleep(net, 0);
				sleep_never = 0;
			}
#endif 
		}
#endif 

		if (dhd->pub.busstate != DHD_BUS_DATA) {

			
			if ((ret = dhd_bus_start(&dhd->pub)) != 0) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				ret = -1;
				goto exit;
			}

		}

		
		memcpy(net->dev_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
		memcpy(dhd->iflist[ifidx]->mac_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);

#ifdef TOE
		
		if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0 && (toe_ol & TOE_TX_CSUM_OL) != 0)
			dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
		else
			dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
#endif 

#if defined(WL_CFG80211)
		if (unlikely(wl_cfg80211_up(NULL))) {
			DHD_ERROR(("%s: failed to bring up cfg80211\n", __FUNCTION__));
			ret = -1;
			goto exit;
		}
#endif 
	}

	
	netif_start_queue(net);
	dhd->pub.up = 1;

	
	if (ifidx == 0)
		dhd_wl_ioctl_cmd(&dhd->pub, WLC_UP, (char *)&up, sizeof(up), TRUE, 0);

#ifdef BCMDBGFS
	dhd_dbg_init(&dhd->pub);
#endif

	OLD_MOD_INC_USE_COUNT;
exit:
    if (ret) {
        wl_android_wifi_off(net);
        dhd_stop(net);
    }

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	if (ret&&(dhd_open_retry_count <3)) {
		dhd_open_retry_count++;
		goto dhd_open_retry;
	}

	return ret;
}

int dhd_do_driver_init(struct net_device *net)
{
	dhd_info_t *dhd = NULL;

	if (!net) {
		DHD_ERROR(("Primary Interface not initialized \n"));
		return -EINVAL;
	}

	dhd = *(dhd_info_t **)netdev_priv(net);

	if (dhd->pub.busstate == DHD_BUS_DATA) {
		DHD_TRACE(("Driver already Inititalized. Nothing to do"));
		return 0;
	}

	if (dhd_open(net) < 0) {
		DHD_ERROR(("Driver Init Failed \n"));
		return -1;
	}

	return 0;
}

osl_t *
dhd_osl_attach(void *pdev, uint bustype)
{
	return osl_attach(pdev, bustype, TRUE);
}

void
dhd_osl_detach(osl_t *osh)
{
	if (MALLOCED(osh)) {
		DHD_ERROR(("%s: MEMORY LEAK %d bytes\n", __FUNCTION__, MALLOCED(osh)));
	}
	osl_detach(osh);
	wifi_fail_retry = true;
#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	dhd_registration_check = FALSE;
	up(&dhd_registration_sem);
#if	defined(BCMLXSDMMC)
#if 0
	up(&dhd_chipup_sem);
#endif
#endif
#endif 
}

int
dhd_add_if(dhd_info_t *dhd, int ifidx, void *handle, char *name,
	uint8 *mac_addr, uint32 flags, uint8 bssidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d, handle->%p\n", __FUNCTION__, ifidx, handle));

	ASSERT(dhd && (ifidx < DHD_MAX_IFS));

	ifp = dhd->iflist[ifidx];
	if (ifp != NULL) {
		if (ifp->net != NULL) {
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			free_netdev(ifp->net);
		}
	} else
		if ((ifp = MALLOC(dhd->pub.osh, sizeof(dhd_if_t))) == NULL) {
			DHD_ERROR(("%s: OOM - dhd_if_t\n", __FUNCTION__));
			return -ENOMEM;
		}

	memset(ifp, 0, sizeof(dhd_if_t));
	ifp->event2cfg80211 = FALSE;
	ifp->info = dhd;
	dhd->iflist[ifidx] = ifp;
	strncpy(ifp->name, name, IFNAMSIZ);
	ifp->name[IFNAMSIZ] = '\0';
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETHER_ADDR_LEN);

	if (handle == NULL) {
		ifp->state = DHD_IF_ADD;
		ifp->idx = ifidx;
		ifp->bssidx = bssidx;
		ASSERT(dhd->thr_sysioc_ctl.thr_pid >= 0);
		up(&dhd->thr_sysioc_ctl.sema);
	} else
		ifp->net = (struct net_device *)handle;

	if (ifidx == 0) {
		ifp->event2cfg80211 = TRUE;
	}

	return 0;
}

void
dhd_del_if(dhd_info_t *dhd, int ifidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d\n", __FUNCTION__, ifidx));

	ASSERT(dhd && ifidx && (ifidx < DHD_MAX_IFS));
	ifp = dhd->iflist[ifidx];
	if (!ifp) {
		DHD_ERROR(("%s: Null interface\n", __FUNCTION__));
		return;
	}

	ifp->state = DHD_IF_DEL;
	ifp->idx = ifidx;
	ASSERT(dhd->thr_sysioc_ctl.thr_pid >= 0);
	up(&dhd->thr_sysioc_ctl.sema);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_open,
	.ndo_stop = dhd_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};

static struct net_device_ops dhd_ops_virt = {
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};
#endif 

extern int system_rev;
#define XA		0
#define XB		1
#define XC		2
#define XD		3
#define PVT		0x80
#define PVT_1	0x81

int
dhd_change_nvram_path(void)
{
printf("Read PCBID = %x\n", system_rev);

#ifdef CONFIG_MACH_MONARUDO
	if (system_rev >= PVT_1){
		strcpy(nvram_path, "/system/etc/calibration.gpio4");
	}
#endif

#ifdef CONFIG_MACH_DUMMY
	if (system_rev >= PVT){
		strcpy(nvram_path, "/system/etc/calibration.gpio4");
	}
#endif

#ifdef CONFIG_MACH_DELUXE_U
	if (system_rev >= PVT){
		strcpy(nvram_path, "/system/etc/calibration.gpio4");
	}
#endif

#ifdef CONFIG_MACH_DUMMY
	if (system_rev >= XC){
		strcpy(nvram_path, "/system/etc/calibration.gpio4");
	}
#endif

#ifdef CONFIG_MACH_DUMMY
	strcpy(nvram_path, "/system/etc/calibration.gpio4");
#endif
return 0;
}

dhd_pub_t *
dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net = NULL;

	dhd_attach_states_t dhd_state = DHD_ATTACH_STATE_INIT;
	DHD_ERROR(("%s: Enter\n", __FUNCTION__));

	
	if (strlen(firmware_path) != 0) {
		strncpy(fw_path, firmware_path, sizeof(fw_path) - 1);
		fw_path[sizeof(fw_path) - 1] = '\0';
	}

	if (bcm_chip_is_4330) {
		if (bcm_chip_is_4330b1) {
			if ((fwb1_path != NULL) && (fwb1_path[0] != '\0'))
				strcpy(fw_path, fwb1_path);
		} else {
			if ((fwb2_path != NULL) && (fwb2_path[0] != '\0'))
				strcpy(fw_path, fwb2_path);
		}
	}
	
	dhd_change_nvram_path();
	
	if (strlen(nvram_path) != 0) {
		strncpy(nv_path, nvram_path, sizeof(nv_path) -1);
		nv_path[sizeof(nv_path) -1] = '\0';
	}
#if defined(SUPPORT_MULTIPLE_REVISION)
	if (strlen(fw_path) != 0 &&
		concate_revision(bus, fw_path, MOD_PARAM_PATHLEN) != 0) {
		DHD_ERROR(("%s: fail to concatnate revison \n", __FUNCTION__));
		goto fail;
	}
#endif

	
	if (!(net = alloc_etherdev(sizeof(dhd)))) {
		DHD_ERROR(("%s: OOM - alloc_etherdev\n", __FUNCTION__));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_NET_ALLOC;

	
	if (!(dhd = MALLOC(osh, sizeof(dhd_info_t)))) {
		DHD_ERROR(("%s: OOM - alloc dhd_info\n", __FUNCTION__));
		goto fail;
	}
	memset(dhd, 0, sizeof(dhd_info_t));

#ifdef DHDTHREAD
	dhd->thr_dpc_ctl.thr_pid = DHD_PID_KT_TL_INVALID;
	dhd->thr_wdt_ctl.thr_pid = DHD_PID_KT_INVALID;
#endif 
	dhd->dhd_tasklet_create = FALSE;
	dhd->thr_sysioc_ctl.thr_pid = DHD_PID_KT_INVALID;
	dhd_state |= DHD_ATTACH_STATE_DHD_ALLOC;

	memcpy((void *)netdev_priv(net), &dhd, sizeof(dhd));
	dhd->pub.osh = osh;

	
	dhd->pub.info = dhd;
	
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;

	
	if (iface_name[0]) {
		int len;
		char ch;
		strncpy(net->name, iface_name, IFNAMSIZ);
		net->name[IFNAMSIZ - 1] = 0;
		len = strlen(net->name);
#ifdef HTC_KlocWork
        if(len > 0) {
            ch = net->name[len - 1];
            if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
                strcat(net->name, "%d");
        }
#else
        ch = net->name[len - 1];
        if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
            strcat(net->name, "%d");
#endif
	}

	if (dhd_add_if(dhd, 0, (void *)net, net->name, NULL, 0, 0) == DHD_BAD_IF)
	{
		printf("%s : dhd_add_if failed\n", __func__);
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_ADD_IF;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif

	sema_init(&dhd->proto_sem, 1);

#ifdef PROP_TXSTATUS
	spin_lock_init(&dhd->wlfc_spinlock);
#endif 

	
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->ctrl_wait);

	
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
	spin_lock_init(&dhd->dhd_lock);

	
	spin_lock_init(&dhd->wakelock_spinlock);
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
	wake_lock_init(&dhd->wl_rxwake, WAKE_LOCK_SUSPEND, "wlan_rx_wake");
	wake_lock_init(&dhd->wl_ctrlwake, WAKE_LOCK_SUSPEND, "wlan_ctrl_wake");
	wake_lock_init(&dhd->wl_htc, WAKE_LOCK_SUSPEND, "wlan_htc");
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_init(&dhd->dhd_net_if_mutex);
	mutex_init(&dhd->dhd_suspend_mutex);
#endif
	dhd_state |= DHD_ATTACH_STATE_WAKELOCKS_INIT;

	
	if (dhd_prot_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_prot_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_PROT_ATTACH;

#ifdef WL_CFG80211
	
	if (unlikely(wl_cfg80211_attach(net, &dhd->pub))) {
		DHD_ERROR(("wl_cfg80211_attach failed\n"));
		goto fail;
	}

	dhd_monitor_init(&dhd->pub);
	dhd_state |= DHD_ATTACH_STATE_CFG80211;
#endif
#if defined(CONFIG_WIRELESS_EXT)
	
	
		if (wl_iw_attach(net, (void *)&dhd->pub) != 0) {
		DHD_ERROR(("wl_iw_attach failed\n"));
		printf("%s : wl_iw_attach failed\n", __func__);
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_WL_ATTACH;
	
#endif 


	
	init_timer(&dhd->timer);
	dhd->timer.data = (ulong)dhd;
	dhd->timer.function = dhd_watchdog;

#ifdef DHDTHREAD
	
	sema_init(&dhd->sdsem, 1);
	if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0)) {
		dhd->threads_only = TRUE;
	}
	else {
		dhd->threads_only = FALSE;
	}

	dhd->dhd_force_exit = FALSE; 
    
	if (dhd_dpc_prio >= 0) {
		
#ifdef USE_KTHREAD_API
		printf("%s: Initialize watchdog thread\n",__func__);
		PROC_START2(dhd_watchdog_thread, dhd, &dhd->thr_wdt_ctl, 0, "dhd_watchdog_thread");
#else
		PROC_START(dhd_watchdog_thread, dhd, &dhd->thr_wdt_ctl, 0);
#endif
	} else {
		dhd->thr_wdt_ctl.thr_pid = -1;
	}

	
	if (dhd_dpc_prio >= 0) {
		
#ifdef USE_KTHREAD_API
		printf("%s: Initialize DPC thread\n",__func__);
		PROC_START2(dhd_dpc_thread, dhd, &dhd->thr_dpc_ctl, 0, "dhd_dpc");
#else
		PROC_START(dhd_dpc_thread, dhd, &dhd->thr_dpc_ctl, 0);
#endif
	} else {
		
		tasklet_init(&dhd->tasklet, dhd_dpc, (ulong)dhd);
		dhd->thr_dpc_ctl.thr_pid = -1;
	}
#else
	
	tasklet_init(&dhd->tasklet, dhd_dpc, (ulong)dhd);
	dhd->dhd_tasklet_create = TRUE;
#endif 

	if (dhd_sysioc) {
#ifdef USE_KTHREAD_API
		printf("%s: Initialize sysioc thread\n",__func__);
		PROC_START2(_dhd_sysioc_thread, dhd, &dhd->thr_sysioc_ctl, 0, "dhd_sysioc");
#else
		PROC_START(_dhd_sysioc_thread, dhd, &dhd->thr_sysioc_ctl, 0);
#endif
	} else {
		dhd->thr_sysioc_ctl.thr_pid = -1;
	}
	dhd_state |= DHD_ATTACH_STATE_THREADS_CREATED;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (1)
	INIT_WORK(&dhd->work_hang, dhd_hang_process);
#endif 
	memcpy(netdev_priv(net), &dhd, sizeof(dhd));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
	register_pm_notifier(&dhd_sleep_pm_notifier);
#endif 

#ifdef CONFIG_HAS_EARLYSUSPEND
	dhd->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 20;
	dhd->early_suspend.suspend = dhd_early_suspend;
	dhd->early_suspend.resume = dhd_late_resume;
	register_early_suspend(&dhd->early_suspend);
	dhd_state |= DHD_ATTACH_STATE_EARLYSUSPEND_DONE;
#endif

#ifdef ARP_OFFLOAD_SUPPORT
	dhd->pend_ipaddr = 0;
	register_inetaddr_notifier(&dhd_notifier);
#endif 

	dhd_state |= DHD_ATTACH_STATE_DONE;
	dhd->dhd_state = dhd_state;
	return &dhd->pub;

fail:
	printf("%s : fail case\n", __func__);
	if (dhd_state < DHD_ATTACH_STATE_DHD_ALLOC) {
		printf("%s : dhd_state < DHD_ATTACH_STATE_DHD_ALLOC\n", __func__);
		if (net) free_netdev(net);
	} else {
		DHD_ERROR(("%s: Calling dhd_detach dhd_state 0x%x &dhd->pub %p\n",
			__FUNCTION__, dhd_state, &dhd->pub));
		dhd->dhd_state = dhd_state;
		dhd_detach(&dhd->pub);
		dhd_free(&dhd->pub);
	}

	return NULL;
}

int
dhd_bus_start(dhd_pub_t *dhdp)
{
	int ret = -1;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;
	unsigned long flags;
#ifdef CUSTOMER_HW2
	char mac_buf[16];
#endif

	ASSERT(dhd);

	DHD_TRACE(("Enter %s:\n", __FUNCTION__));

#ifdef DHDTHREAD
	if (dhd->threads_only)
		dhd_os_sdlock(dhdp);
#endif 

	
	if  ((dhd->pub.busstate == DHD_BUS_DOWN) &&
		(fw_path != NULL) && (fw_path[0] != '\0') &&
		(nv_path != NULL) && (nv_path[0] != '\0')) {
		printf("load firmware from %s, nvram %s\n", fw_path, nv_path);
		
		if (!(dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
		                                fw_path, nv_path))) {
			DHD_ERROR(("%s: dhdsdio_probe_download failed. firmware = %s nvram = %s\n",
			           __FUNCTION__, fw_path, nv_path));
#ifdef DHDTHREAD
			if (dhd->threads_only)
				dhd_os_sdunlock(dhdp);
#endif 
			return -1;
		}
	}
	if (dhd->pub.busstate != DHD_BUS_LOAD) {
#ifdef DHDTHREAD
		if (dhd->threads_only)
			dhd_os_sdunlock(dhdp);
#endif 
		return -ENETDOWN;
	}

	
	dhd->pub.tickcnt = 0;
	dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);

	
	if ((ret = dhd_bus_init(&dhd->pub, FALSE)) != 0) {

		DHD_ERROR(("%s, dhd_bus_init failed %d\n", __FUNCTION__, ret));
#ifdef DHDTHREAD
		if (dhd->threads_only)
			dhd_os_sdunlock(dhdp);
#endif 
		return ret;
	}
#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID)
	
	if (bcmsdh_register_oob_intr(dhdp)) {
		

		flags = dhd_os_spin_lock(&dhd->pub);
		dhd->wd_timer_valid = FALSE;
		dhd_os_spin_unlock(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);

		DHD_ERROR(("%s Host failed to register for OOB\n", __FUNCTION__));
#ifdef DHDTHREAD
		if (dhd->threads_only)
			dhd_os_sdunlock(dhdp);
#endif 
		return -ENODEV;
	}

#ifndef BCMSPI_ANDROID
	
	dhd_enable_oob_intr(dhd->pub.bus, TRUE);
#endif 
#endif 

	
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		flags = dhd_os_spin_lock(&dhd->pub);
		dhd->wd_timer_valid = FALSE;
		dhd_os_spin_unlock(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);
		DHD_ERROR(("%s failed bus is not ready\n", __FUNCTION__));
#ifdef DHDTHREAD
		if (dhd->threads_only)
			dhd_os_sdunlock(dhdp);
#endif 
		return -ENODEV;
	}

#ifdef DHDTHREAD
	if (dhd->threads_only)
		dhd_os_sdunlock(dhdp);
#endif 

#ifdef BCMSDIOH_TXGLOM
	if ((dhd->pub.busstate == DHD_BUS_DATA) && bcmsdh_glom_enabled()) {
		dhd_txglom_enable(dhdp, TRUE);
	}
#endif
#ifdef READ_MACADDR
	dhd_read_macaddr(dhd);
#endif

	
	if ((ret = dhd_prot_init(&dhd->pub)) < 0)
		return ret;

#ifdef CUSTOMER_HW2
	if (ap_fw_loaded == FALSE) {
		sprintf( mac_buf, "0x%02x%02x%02x%02x%02x%02x",
	        dhdp->mac.octet[0], dhdp->mac.octet[1], dhdp->mac.octet[2],
        	dhdp->mac.octet[3], dhdp->mac.octet[4], dhdp->mac.octet[5] );
		dhdp->pktfilter_count = 3;
		
#if 0
		dhd_set_pktfilter(dhdp, 1, ALLOW_UNICAST, 0, "0xffffffffffff", mac_buf);
		dhd_set_pktfilter(dhdp, 1, ALLOW_DHCP, 0, "0xffffffffffff000000000000ffff00000000000000000000000000000000000000000000ffff", "0xffffffffffff0000000000000800000000000000000000000000000000000000000000000044");
		dhd_set_pktfilter(dhdp, 1, ALLOW_IPV6_MULTICAST, 0, "0xffff", "0x3333");
#endif
	}
#else
	dhdp->pktfilter_count = 1;
	
	dhdp->pktfilter[0] = "100 0 0 0 0x01 0x00";
#endif
#ifdef WRITE_MACADDR
	dhd_write_macaddr(dhd->pub.mac.octet);
#endif

	priv_dhdp = dhdp;
#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->pend_ipaddr) {
#ifdef AOE_IP_ALIAS_SUPPORT
		aoe_update_host_ipv4_table(&dhd->pub, dhd->pend_ipaddr, TRUE);
#endif 
		dhd->pend_ipaddr = 0;
	}
#endif 

	return 0;
}


#define TRAFFIC_HIGH_WATER_MARK                670 *(3000/1000)
#define TRAFFIC_LOW_WATER_MARK          280 * (3000/1000)
#define TRAFFIC_S_HIGH_WATER_MARK	((TRAFFIC_HIGH_WATER_MARK) * 20)

extern dhd_pub_t *pdhd;

#if 1
bool
dhd_concurrent_fw(dhd_pub_t *dhd)
{
#ifdef WL_ENABLE_P2P_IF
	if (strstr(fw_path, "_apsta") == NULL)
		return 1;
	else
		return 0;
#else 
	int i;
	int ret = 0;
	char buf[WLC_IOCTL_SMLEN];
	char *cap[] = {"p2p", "mchan", "dsta", NULL};

	if ((!op_mode) && (strstr(fw_path, "_p2p") == NULL) &&
		(strstr(fw_path, "_apsta") == NULL)) {
		for (i = 0; cap[i] != NULL; ) {
			memset(buf, 0, sizeof(buf));
			bcm_mkiovar(cap[i++], 0, 0, buf, sizeof(buf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf),
				FALSE, 0)) < 0) {
				DHD_TRACE(("%s: Get VSDB Capability(%s) failed (error=%d)\n",
					__FUNCTION__, cap[i-1], ret));
				return 0;
			} else if (buf[0] != 1) {
				DHD_TRACE(("VSDB(%s) is not supported , ret : %d\n",
					cap[i-1], buf[0]));
				return 0;
			}
		}
		return 1;
	}
	return 0;
#endif 
}
#endif 
int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	int ret = 0;
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	

	char buf[256];
	char *ptr;
	uint up = 0;
	uint power_mode = PM_FAST;
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = CUSTOM_GLOM_SETTING;
	uint32 txlazydelay = 0;
	uint32 lvtrigtime = 6;
#if defined(CONFIG_MACH_DUMMY)
	uint bcn_timeout = 2;
#elif defined(VSDB) || defined(ROAM_ENABLE)
	uint bcn_timeout = 8;
#else
	uint bcn_timeout = 6;
#endif

	uint retry_max = 10;
#if defined(ARP_OFFLOAD_SUPPORT)
	int arpoe = 0; 
#endif
	int scan_assoc_time = 40;
	int scan_unassoc_time = 80;
	int scan_passive_time = 100;
	uint32 listen_interval = LISTEN_INTERVAL;  
	uint16 chipID;
#if defined(SOFTAP)
	uint dtim = 1;
#endif
#if (defined(AP) && !defined(WLP2P)) || (!defined(AP) && defined(WL_CFG80211))
	uint32 mpc = 0; 
	struct ether_addr p2p_ea;
#endif

int ht_wsec_restrict = WLC_HT_TKIP_RESTRICT | WLC_HT_WEP_RESTRICT;
#ifdef CUSTOMER_HW2
	uint dhd_roam = 0;
#else
	uint dhd_roam = 1;
#endif

#if defined(AP) || defined(WLP2P)  || defined(APSTA_CONCURRENT)
	uint32 apsta = 1; 
#endif 
#ifdef GET_CUSTOM_MAC_ENABLE
	struct ether_addr ea_addr;
#endif 
#ifdef OKC_SUPPORT
	uint32 okc = 1;
#endif
	uint srl = 15;
	uint lrl = 15;

	DHD_TRACE(("Enter %s\n", __FUNCTION__));
	dhd->op_mode = 0;
#ifdef GET_CUSTOM_MAC_ENABLE
	ret = dhd_custom_get_mac_address(ea_addr.octet);
	if (!ret) {
		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("cur_etheraddr", (void *)&ea_addr, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			return BCME_NOTUP;
		}
		memcpy(dhd->mac.octet, ea_addr.octet, ETHER_ADDR_LEN);
	} else {
#endif 
	pdhd = dhd;
#ifdef PNO_SUPPORT
	
	dhd_clear_pfn();
#endif

	
	strcpy(iovbuf, "cur_etheraddr");
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
		return BCME_NOTUP;
	}
	memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);

#ifdef GET_CUSTOM_MAC_ENABLE
	}
#endif 

#ifdef SET_RANDOM_MAC_SOFTAP
	if ((!op_mode && strstr(fw_path, "_apsta") != NULL) || (op_mode == HOSTAPD_MASK)) {
		uint rand_mac;

		srandom32((uint)jiffies);
		rand_mac = random32();
		iovbuf[0] = 0x02;              
		iovbuf[1] = 0x1A;
		iovbuf[2] = 0x11;
		iovbuf[3] = (unsigned char)(rand_mac & 0x0F) | 0xF0;
		iovbuf[4] = (unsigned char)(rand_mac >> 8);
		iovbuf[5] = (unsigned char)(rand_mac >> 16);

		bcm_mkiovar("cur_etheraddr", (void *)iovbuf, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
		} else
			memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);
	}
#endif 

	DHD_TRACE(("Firmware = %s\n", fw_path));

#ifdef APSTA_CONCURRENT
	if (strstr(fw_path, "_apsta") == NULL) {
		bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
				DHD_ERROR(("%s APSTA for apsta_concurrent failed ret= %d\n", __FUNCTION__, ret));
		}
	}
#endif
#if !defined(AP) && defined(WLP2P)
	
	if ((!op_mode && strstr(fw_path, "_p2p") != NULL) || (op_mode == 0x04) ||
		(dhd_concurrent_fw(dhd))) {
		bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s APSTA for WFD failed ret= %d\n", __FUNCTION__, ret));
		} else if (!dhd_concurrent_fw(dhd)) {
			dhd->op_mode |= WFD_MASK;
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif 
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif
		}
		memcpy(&p2p_ea, &dhd->mac, ETHER_ADDR_LEN);
		ETHER_SET_LOCALADDR(&p2p_ea);
		bcm_mkiovar("p2p_da_override", (char *)&p2p_ea,
			ETHER_ADDR_LEN, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s p2p_da_override ret= %d\n", __FUNCTION__, ret));
		} else {
			DHD_INFO(("dhd_preinit_ioctls: p2p_da_override succeeded\n"));
		}
	}
#endif 

#if !defined(AP) && defined(WL_CFG80211)
	
	if ((!op_mode && strstr(fw_path, "_apsta") != NULL) || (op_mode == 0x02)) {
			
			bcm_mkiovar("mpc", (char *)&mpc, 4, iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
				sizeof(iovbuf), TRUE, 0)) < 0) {
				DHD_ERROR(("%s mpc for HostAPD failed  %d\n", __FUNCTION__, ret));
			} else {
				dhd->op_mode |= HOSTAPD_MASK;
#if defined(ARP_OFFLOAD_SUPPORT)
				arpoe = 0;
#endif 
#ifdef PKT_FILTER_SUPPORT
				dhd_pkt_filter_enable = FALSE;
#endif
			}
	}
#endif 

	if ((dhd->op_mode != WFD_MASK) && (dhd->op_mode != HOSTAPD_MASK)) {
		
		dhd->op_mode |= STA_MASK;
#ifdef PKT_FILTER_SUPPORT
		dhd_pkt_filter_enable = TRUE;
#endif
#if !defined(AP) && defined(WLP2P)
		if (dhd_concurrent_fw(dhd)) {
			dhd->op_mode |= WFD_MASK;
		}
#endif
	}

	DHD_ERROR(("Firmware up: op_mode=%d, "
			"Broadcom Dongle Host Driver mac=%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
			dhd->op_mode,
			dhd->mac.octet[0], dhd->mac.octet[1], dhd->mac.octet[2],
			dhd->mac.octet[3], dhd->mac.octet[4], dhd->mac.octet[5]));

	
	if (dhd->dhd_cspec.ccode[0] != 0) {
		bcm_mkiovar("country", (char *)&dhd->dhd_cspec,
			sizeof(wl_country_t), iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
	}

	
	bcm_mkiovar("assoc_listen", (char *)&listen_interval, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s assoc_listen failed %d\n", __FUNCTION__, ret));

#ifdef OKC_SUPPORT
	bcm_mkiovar("okc_enable", (char *)&okc, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif

	
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	bcm_mkiovar("ver", (char *)&buf, 4, buf, sizeof(buf));
	dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf),WL_IOCTL_ACTION_SET);
	bcmstrtok(&ptr, "\n", 0);
	
	DHD_DEFAULT(("Firmware version = %s\n", buf));
#ifdef CONFIG_CONTROL_PM
	sec_control_pm(dhd, &power_mode);
#else
	
	dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);
#endif

	
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	
	chipID = (uint16)dhd_bus_chip_id(dhd);
	
		DHD_INFO(("%s disable glom for chipID=0x%X\n", __FUNCTION__, chipID));
		bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	
	bcm_mkiovar("bus:txlazydelay", (char *)&txlazydelay, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	
	bcm_mkiovar("bus:lvtrigtime", (char *)&lvtrigtime, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	
	bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	
	bcm_mkiovar("roam_off", (char *)&dhd_roam, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#if defined(AP) && !defined(WLP2P)
	
	bcm_mkiovar("mpc", (char *)&mpc, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 

#if defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim), TRUE, 0);
	}
#endif

	if (dhd_roam == 0)
	{
		
		int roam_scan_period = 30; 
		int roam_fullscan_period = 120; 
#ifdef CUSTOMER_HW2
		int roam_trigger = -75;
		int roam_delta = 10;
#else
		int roam_trigger = -85;
		int roam_delta = 15;
#endif
		int band;
		int band_temp_set = WLC_BAND_2G;

		if (dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD, \
			(char *)&roam_scan_period, sizeof(roam_scan_period),TRUE,0) < 0)
			DHD_ERROR(("%s: roam scan setup failed\n", __FUNCTION__));

		bcm_mkiovar("fullroamperiod", (char *)&roam_fullscan_period, \
					 4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, \
			iovbuf, sizeof(iovbuf), TRUE, 0) < 0)
			DHD_ERROR(("%s: roam fullscan setup failed\n", __FUNCTION__));

		if (dhdcdc_query_ioctl(dhd, 0, WLC_GET_BAND, \
				(char *)&band, sizeof(band),WL_IOCTL_ACTION_GET) < 0)
			DHD_ERROR(("%s: roam delta setting failed\n", __FUNCTION__));
		else {
			if ((band == WLC_BAND_AUTO) || (band == WLC_BAND_ALL))
			{
				
				if (dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, \
					(char *)&band_temp_set, sizeof(band_temp_set), TRUE, 0) < 0)
					DHD_ERROR(("%s: local band seting failed\n", __FUNCTION__));
			}
			if (dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, \
				(char *)&roam_delta, sizeof(roam_delta), TRUE, 0) < 0)
				DHD_ERROR(("%s: roam delta setting failed\n", __FUNCTION__));

			if (dhd_wl_ioctl_cmd(dhd,  WLC_SET_ROAM_TRIGGER, \
				(char *)&roam_trigger, sizeof(roam_trigger), TRUE, 0) < 0)
				DHD_ERROR(("%s: roam trigger setting failed\n", __FUNCTION__));

			
			
			band_temp_set = WLC_BAND_5G;
			roam_delta = 10;
			roam_trigger = -75;
			if (dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, \
				(char *)&band_temp_set, sizeof(band_temp_set), TRUE, 0) < 0)
				DHD_ERROR(("%s: local band seting failed\n", __FUNCTION__));

			if (dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, \
				(char *)&roam_delta, sizeof(roam_delta), TRUE, 0) < 0)
				DHD_ERROR(("%s: 5G roam delta setting failed\n", __FUNCTION__));

			if (dhd_wl_ioctl_cmd(dhd,  WLC_SET_ROAM_TRIGGER, \
				(char *)&roam_trigger, sizeof(roam_trigger), TRUE, 0) < 0)
				DHD_ERROR(("%s: 5G roam trigger setting failed\n", __FUNCTION__));
			

			
			if (dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, \
				(char *)&band, sizeof(band), TRUE, 0) < 0)
				DHD_ERROR(("%s: Original band restore failed\n", __FUNCTION__));
		}
	}

#if defined(KEEP_ALIVE)
	{
	
	int res;

#if defined(SOFTAP)
	if (ap_fw_loaded == FALSE)
#endif 
		if ((dhd->op_mode & HOSTAPD_MASK) != HOSTAPD_MASK) {
			if ((res = dhd_keep_alive_onoff(dhd)) < 0)
				DHD_ERROR(("%s set keeplive failed %d\n",
				__FUNCTION__, res));
		}
	}
#endif 

	
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

	
	setbit(eventmask, WLC_E_SET_SSID);
	setbit(eventmask, WLC_E_PRUNE);
	setbit(eventmask, WLC_E_AUTH);
	setbit(eventmask, WLC_E_REASSOC);
	setbit(eventmask, WLC_E_REASSOC_IND);
	setbit(eventmask, WLC_E_DEAUTH);
	setbit(eventmask, WLC_E_DEAUTH_IND);
	setbit(eventmask, WLC_E_DISASSOC_IND);
	setbit(eventmask, WLC_E_DISASSOC);
	setbit(eventmask, WLC_E_JOIN);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_NDIS_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
	setbit(eventmask, WLC_E_ASSOC_REQ_IE);
	setbit(eventmask, WLC_E_ASSOC_RESP_IE);
	setbit(eventmask, WLC_E_PMKID_CACHE);
	
	setbit(eventmask, WLC_E_JOIN_START);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
	
#ifdef WLMEDIA_HTSF
	setbit(eventmask, WLC_E_HTSFSYNC);
#endif 
#ifdef PNO_SUPPORT
	setbit(eventmask, WLC_E_PFN_NET_FOUND);
#endif 
#ifdef WLC_E_RSSI_LOW 
	setbit(eventmask, WLC_E_RSSI_LOW);
#endif 
	
	setbit(eventmask, WLC_E_ROAM);
	
	setbit(eventmask, WLC_E_LOAD_IND);
#if defined(HTC_TX_TRACKING)
	setbit(eventmask, WLC_E_TX_STAT_ERROR);
#endif
#ifdef WL_CFG80211
	setbit(eventmask, WLC_E_ESCAN_RESULT);
	
	if(!is_screen_off) {
		setbit(eventmask, WLC_E_ACTION_FRAME_RX);
		setbit(eventmask, WLC_E_ACTION_FRAME_COMPLETE);
		setbit(eventmask, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE);
		setbit(eventmask, WLC_E_P2P_PROBREQ_MSG);
		setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);
	}
	else
	{
		printf("screen is off, don't register some events\n");
	}
	
#endif 

	
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}

	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_PASSIVE_TIME, (char *)&scan_passive_time,
		sizeof(scan_passive_time), TRUE, 0);

    
    bcm_mkiovar("ht_wsec_restrict", (char *)&ht_wsec_restrict, 4, iovbuf, sizeof(iovbuf));
    dhd_wl_ioctl_cmd(dhd,WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

    
    bcm_mkiovar("assoc_retry_max", (char *)&retry_max, 4, iovbuf, sizeof(iovbuf));
    dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

    

#ifdef ARP_OFFLOAD_SUPPORT
	
#if defined(SOFTAP)
	if (arpoe && !ap_fw_loaded) {
#else
	if (arpoe) {
#endif 
		dhd_arp_offload_enable(dhd, TRUE);
		dhd_arp_offload_set(dhd, dhd_arp_mode);
	} else {
		dhd_arp_offload_enable(dhd, FALSE);
		dhd_arp_offload_set(dhd, 0);
	}
#endif 

	
	ret = 3;
	bcm_mkiovar("tc_period", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
#if defined(DHD_BCM_WIFI_HDMI)
	if (dhd_bcm_whdmi_enable) {
		DHD_ERROR(("DHD WiFi HDMI is enabled\n"));

#if !defined(AP) && !defined(WLP2P)
		
		bcm_mkiovar("mpc", (char *)&mpc, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
		bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 

		
		power_mode = PM_OFF;
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);

#ifdef ARP_OFFLOAD_SUPPORT
		
		dhd_arp_offload_set(dhd, 0);
		dhd_arp_offload_enable(dhd, FALSE);
#endif 

	} else {
		
	}
#endif 	
	ret = dhd_wl_ioctl_cmd(dhd, WLC_UP, (char *)&up, sizeof(up), TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s Setting WL UP failed %d\n", __FUNCTION__, ret));
		goto done;
	}
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = TRAFFIC_LOW_WATER_MARK;
	bcm_mkiovar("tc_lo_wm", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = TRAFFIC_HIGH_WATER_MARK;
	bcm_mkiovar("tc_hi_wm", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = TRAFFIC_S_HIGH_WATER_MARK;
	bcm_mkiovar("tc_shi_wm", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = 1;
	bcm_mkiovar("tc_enable", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#if defined(HTC_TX_TRACKING)
{
		
		uint tx_stat_chk = 0; 
		uint tx_stat_chk_prd = 5; 
		uint tx_stat_chk_ratio = 8; 
		uint tx_stat_chk_num = 5; 

		bcm_mkiovar("tx_stat_chk_num", (char *)&tx_stat_chk_num, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

		bcm_mkiovar("tx_stat_chk_ratio", (char *)&tx_stat_chk_ratio, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

		bcm_mkiovar("tx_stat_chk_prd", (char *)&tx_stat_chk_prd, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

		bcm_mkiovar("tx_stat_chk", (char *)&tx_stat_chk, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
}
#endif

	
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SRL, (char *)&srl, sizeof(srl), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_LRL, (char *)&lrl, sizeof(lrl), TRUE, 0);

#ifdef GAN_LITE_NAT_KEEPALIVE_FILTER
	
	dhd->pktfilter[0] = "100 0 0 0 0xffffff 0xffffff"; 
	dhd->pktfilter[1] = "102 0 0 36 0xffffffff 0x11940009"; 
	dhd->pktfilter[2] = "104 0 0 38 0xffffffff 0x11940009"; 
	dhd->pktfilter[3] = NULL;
#endif


done:
	return ret;
}


int
dhd_iovar(dhd_pub_t *pub, int ifidx, char *name, char *cmd_buf, uint cmd_len, int set)
{
	char buf[strlen(name) + 1 + cmd_len];
	int len = sizeof(buf);
	wl_ioctl_t ioc;
	int ret;

	len = bcm_mkiovar(name, cmd_buf, cmd_len, buf, len);

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = set? WLC_SET_VAR : WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (!set && ret >= 0)
		memcpy(cmd_buf, buf, cmd_len);

	return ret;
}

int dhd_change_mtu(dhd_pub_t *dhdp, int new_mtu, int ifidx)
{
	struct dhd_info *dhd = dhdp->info;
	struct net_device *dev = NULL;

	ASSERT(dhd && dhd->iflist[ifidx]);
	dev = dhd->iflist[ifidx]->net;
	ASSERT(dev);

	if (netif_running(dev)) {
		DHD_ERROR(("%s: Must be down to change its MTU", dev->name));
		return BCME_NOTDOWN;
	}

#define DHD_MIN_MTU 1500
#define DHD_MAX_MTU 1752

	if ((new_mtu < DHD_MIN_MTU) || (new_mtu > DHD_MAX_MTU)) {
		DHD_ERROR(("%s: MTU size %d is invalid.\n", __FUNCTION__, new_mtu));
		return BCME_BADARG;
	}

	dev->mtu = new_mtu;
	return 0;
}

#ifdef ARP_OFFLOAD_SUPPORT
void
aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add)
{
	u32 ipv4_buf[MAX_IPV4_ENTRIES]; 
	int i;
	int ret;

	bzero(ipv4_buf, sizeof(ipv4_buf));

	
	ret = dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf));
	DHD_ARPOE(("%s: hostip table read from Dongle:\n", __FUNCTION__));
#ifdef AOE_DBG
	dhd_print_buf(ipv4_buf, 32, 4); 
#endif
	
	dhd_aoe_hostip_clr(dhd_pub);

	if (ret) {
		DHD_ERROR(("%s failed\n", __FUNCTION__));
		return;
	}

	for (i = 0; i < MAX_IPV4_ENTRIES; i++) {
		if (add && (ipv4_buf[i] == 0)) {
				ipv4_buf[i] = ipa;
				add = FALSE; 
				DHD_ARPOE(("%s: Saved new IP in temp arp_hostip[%d]\n",
				__FUNCTION__, i));
		} else if (ipv4_buf[i] == ipa) {
			ipv4_buf[i]	= 0;
			DHD_ARPOE(("%s: removed IP:%x from temp table %d\n",
				__FUNCTION__, ipa, i));
		}

		if (ipv4_buf[i] != 0) {
			
			dhd_arp_offload_add_ip(dhd_pub, ipv4_buf[i]);
			DHD_ARPOE(("%s: added IP:%x to dongle arp_hostip[%d]\n\n",
				__FUNCTION__, ipv4_buf[i], i));
		}
	}
#ifdef AOE_DBG
	
	dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf));
	DHD_ARPOE(("%s: read back arp_hostip table:\n", __FUNCTION__));
	dhd_print_buf(ipv4_buf, 32, 4); 
#endif
}

static int dhd_device_event(struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	dhd_info_t *dhd;
	dhd_pub_t *dhd_pub;

	if (!ifa)
		return NOTIFY_DONE;

	dhd = *(dhd_info_t **)netdev_priv(ifa->ifa_dev->dev);
	dhd_pub = &dhd->pub;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	if (ifa->ifa_dev->dev->netdev_ops == &dhd_ops_pri) {
#else
	if (ifa->ifa_dev->dev) {
#endif
		switch (event) {
		case NETDEV_UP:
			DHD_ARPOE(("%s: [%s] Up IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));

			if (dhd->pub.busstate != DHD_BUS_DATA) {
				DHD_ERROR(("%s: bus not ready, exit\n", __FUNCTION__));
				if (dhd->pend_ipaddr) {
					DHD_ERROR(("%s: overwrite pending ipaddr: 0x%x\n",
						__FUNCTION__, dhd->pend_ipaddr));
				}
				dhd->pend_ipaddr = ifa->ifa_address;
				break;
			}
			
			if (dhd->pub.busstate == DHD_BUS_DOWN) {
				DHD_ERROR(("%s: bus is down, exit\n", __FUNCTION__));
				break;
			}

#ifdef AOE_IP_ALIAS_SUPPORT
			if ((dhd_pub->op_mode & HOSTAPD_MASK) != HOSTAPD_MASK) {
				if (ifa->ifa_label[strlen(ifa->ifa_label)-2] == 0x3a) {
					
					DHD_ARPOE(("%s:add aliased IP to AOE hostip cache\n",
						__FUNCTION__));
					aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, TRUE);
				}
				else
					aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, TRUE);
			}
#endif
			break;

		case NETDEV_DOWN:
			DHD_ARPOE(("%s: [%s] Down IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));
			dhd->pend_ipaddr = 0;
#ifdef AOE_IP_ALIAS_SUPPORT
		if ((dhd_pub->op_mode & HOSTAPD_MASK) != HOSTAPD_MASK) {
			if (!(ifa->ifa_label[strlen(ifa->ifa_label)-2] == 0x3a)) {
					
					DHD_ARPOE(("%s: primary interface is down, AOE clr all\n",
						__FUNCTION__));
					dhd_aoe_hostip_clr(&dhd->pub);
					dhd_aoe_arp_clr(&dhd->pub);
			} else
				aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, FALSE);
		}
#else
			dhd_aoe_hostip_clr(&dhd->pub);
			dhd_aoe_arp_clr(&dhd->pub);
#endif
			break;

		default:
			DHD_ARPOE(("%s: do noting for [%s] Event: %lu\n",
				__func__, ifa->ifa_label, event));
			break;
		}
	}
	return NOTIFY_DONE;
}
#endif 

int
dhd_net_attach(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct net_device *net = NULL;
	int err = 0;
	uint8 temp_addr[ETHER_ADDR_LEN] = { 0x00, 0x90, 0x4c, 0x11, 0x22, 0x33 };

	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	ASSERT(dhd && dhd->iflist[ifidx]);

	net = dhd->iflist[ifidx]->net;
	ASSERT(net);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	ASSERT(!net->open);
	net->get_stats = dhd_get_stats;
	net->do_ioctl = dhd_ioctl_entry;
	net->hard_start_xmit = dhd_start_xmit;
	net->set_mac_address = dhd_set_mac_address;
	net->set_multicast_list = dhd_set_multicast_list;
	net->open = net->stop = NULL;
#else
	ASSERT(!net->netdev_ops);
	net->netdev_ops = &dhd_ops_virt;
#endif 

	
	if (ifidx == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
		net->open = dhd_open;
		net->stop = dhd_stop;
#else
		net->netdev_ops = &dhd_ops_pri;
#endif 
		memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
	} else {
		memcpy(temp_addr, dhd->iflist[ifidx]->mac_addr, ETHER_ADDR_LEN);
		if (!memcmp(temp_addr, dhd->iflist[0]->mac_addr,
			ETHER_ADDR_LEN)) {
			DHD_ERROR(("%s interface [%s]: set locally administered bit in MAC\n",
			__func__, net->name));
			temp_addr[0] |= 0x02;
		}
	}

	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &dhd_ethtool_ops;
#endif 

#if defined(CONFIG_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif 
#if WIRELESS_EXT > 12
	net->wireless_handlers = (struct iw_handler_def *)&wl_iw_handler_def;
#endif 
#endif 

	dhd->pub.rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if ((err = register_netdev(net)) != 0) {
		DHD_ERROR(("couldn't register the net device, err %d\n", err));
		goto fail;
	}
	printf("Broadcom Dongle Host Driver: register interface [%s]"
		" MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		net->name,
		net->dev_addr[0], net->dev_addr[1], net->dev_addr[2],
		net->dev_addr[3], net->dev_addr[4], net->dev_addr[5]);

#if defined(SOFTAP) && defined(CONFIG_WIRELESS_EXT) && !defined(WL_CFG80211)
		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#endif

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if (ifidx == 0) {
		dhd_registration_check = TRUE;
		up(&dhd_registration_sem);
	}
#endif 
	return 0;

fail:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif
	return err;
}

void
dhd_bus_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd) {

			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				
				dhd_prot_stop(&dhd->pub);

				
				dhd_bus_stop(dhd->pub.bus, TRUE);
			}

#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID)
			bcmsdh_unregister_oob_intr();
#endif 
		}
	}
}


void dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	unsigned long flags;
	int timer_valid = FALSE;

	if (!dhdp)
		return;

	dhd = (dhd_info_t *)dhdp->info;
	if (!dhd)
		return;

	dhd->dhd_force_exit = TRUE; 

	DHD_TRACE(("%s: Enter state 0x%x\n", __FUNCTION__, dhd->dhd_state));

	dhd->pub.up = 0;
	if (!(dhd->dhd_state & DHD_ATTACH_STATE_DONE)) {
		osl_delay(1000*100);
	}

	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {
		dhd_bus_detach(dhdp);

		if (dhdp->prot)
			dhd_prot_detach(dhdp);
	}
#ifdef ARP_OFFLOAD_SUPPORT
	unregister_inetaddr_notifier(&dhd_notifier);
#endif 

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	if (dhd->dhd_state & DHD_ATTACH_STATE_EARLYSUSPEND_DONE) {
		if (dhd->early_suspend.suspend)
			unregister_early_suspend(&dhd->early_suspend);
	}
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	cancel_work_sync(&dhd->work_hang);
#endif 

#if defined(CONFIG_WIRELESS_EXT)
	if (dhd->dhd_state & DHD_ATTACH_STATE_WL_ATTACH) {
		
		wl_iw_detach();
	}
#endif 

	if (dhd->thr_sysioc_ctl.thr_pid >= 0) {
		PROC_STOP(&dhd->thr_sysioc_ctl);
	}

	
	if (dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) {
		int i = 1;
		dhd_if_t *ifp;

		
		for (i = 1; i < DHD_MAX_IFS; i++) {
			dhd_net_if_lock_local(dhd);
			if (dhd->iflist[i]) {
				dhd->iflist[i]->state = DHD_IF_DEL;
				dhd->iflist[i]->idx = i;
				dhd_op_if(dhd->iflist[i]);
			}

			dhd_net_if_unlock_local(dhd);
		}
		
		ifp = dhd->iflist[0];
		ASSERT(ifp);

#ifdef HTC_KlocWork
		if (ifp && ifp->net) {
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
			if (ifp->net->open)
#else
			if (ifp->net->netdev_ops == &dhd_ops_pri)
#endif
			{
				if (ifp->net) {
					unregister_netdev(ifp->net);
					
					free_netdev(ifp->net);
					
					ifp->net = NULL;
				}
				MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
				dhd->iflist[0] = NULL;
			}
#ifdef HTC_KlocWork
		}
#endif
	}

	
	flags = dhd_os_spin_lock(&dhd->pub);
	timer_valid = dhd->wd_timer_valid;
	dhd->wd_timer_valid = FALSE;
	dhd_os_spin_unlock(&dhd->pub, flags);
	if (timer_valid)
		del_timer_sync(&dhd->timer);

	if (dhd->dhd_state & DHD_ATTACH_STATE_THREADS_CREATED) {
#ifdef DHDTHREAD
		if (dhd->thr_wdt_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_wdt_ctl);
		}

		if (dhd->thr_dpc_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_dpc_ctl);
		}

		if (dhd->thr_sysioc_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_sysioc_ctl);
		}
		else
#endif 
		tasklet_kill(&dhd->tasklet);
	}
	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {
		dhd_bus_detach(dhdp);

		if (dhdp->prot)
			dhd_prot_detach(dhdp);
	}

#ifdef WL_CFG80211
	if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
		wl_cfg80211_detach(NULL);
		dhd_monitor_uninit();
	}
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
		unregister_pm_notifier(&dhd_sleep_pm_notifier);
#endif 
	

	if (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd->wakelock_counter = 0;
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		wake_lock_destroy(&dhd->wl_wifi);
		wake_lock_destroy(&dhd->wl_rxwake);
		wake_lock_destroy(&dhd->wl_ctrlwake);
		wake_lock_destroy(&dhd->wl_htc);
#endif
	}
	dhd->dhd_force_exit = FALSE; 
}


void
dhd_free(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		int i;
		for (i = 0; i < ARRAYSIZE(dhdp->reorder_bufs); i++) {
			if (dhdp->reorder_bufs[i]) {
				reorder_info_t *ptr;
				uint32 buf_size = sizeof(struct reorder_info);

				ptr = dhdp->reorder_bufs[i];

				buf_size += ((ptr->max_idx + 1) * sizeof(void*));
				DHD_REORDER(("free flow id buf %d, maxidx is %d, buf_size %d\n",
					i, ptr->max_idx, buf_size));

				MFREE(dhdp->osh, dhdp->reorder_bufs[i], buf_size);
				dhdp->reorder_bufs[i] = NULL;
			}
		}
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd)
			MFREE(dhd->pub.osh, dhd, sizeof(*dhd));
	}
}

extern void disable_dev_wlc_ioctl(void);
static void __exit
dhd_module_cleanup(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (priv_dhdp)
		dhd_net_if_lock_local(priv_dhdp->info);

#ifdef CONFIG_WIRELESS_EXT
	disable_dev_wlc_ioctl();
#endif

	module_remove = 1; 
	module_insert = 0;

	if (priv_dhdp)
		dhd_net_if_unlock_local(priv_dhdp->info);

	msleep(1000);
	dhd_bus_unregister();

#if defined(CONFIG_WIFI_CONTROL_FUNC)
	wl_android_wifictrl_func_del();
#endif 
	wl_android_exit();

	
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
    printf("[ATS][press_widget][turn_off]\n"); 
}


static int __init
dhd_module_init(void)
{
	int error = 0;
	int retry = 0;
	wifi_fail_retry = false;
#if 0 && defined(BCMLXSDMMC) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	int retry = POWERUP_MAX_RETRY;
	int chip_up = 0;
#endif 

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	wl_android_init();
init_retry:
#if defined(DHDTHREAD)
	
	do {
		
		if ((dhd_watchdog_prio < 0) && (dhd_dpc_prio < 0))
			break;

		
		if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0) && dhd_deferred_tx)
			break;

		DHD_ERROR(("Invalid module parameters.\n"));
		return -EINVAL;
	} while (0);
#endif 
#if 0 && defined(BCMLXSDMMC) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	do {
		sema_init(&dhd_chipup_sem, 0);
		dhd_bus_reg_sdio_notify(&dhd_chipup_sem);
		dhd_customer_gpio_wlan_ctrl(WLAN_POWER_ON);
#if defined(CONFIG_WIFI_CONTROL_FUNC)
		if (wl_android_wifictrl_func_add() < 0) {
			dhd_bus_unreg_sdio_notify();
			goto fail_1;
		}
#endif 
		if (down_timeout(&dhd_chipup_sem,
			msecs_to_jiffies(POWERUP_WAIT_MS)) == 0) {
			dhd_bus_unreg_sdio_notify();
			chip_up = 1;
			break;
		}
		DHD_ERROR(("\nfailed to power up wifi chip, retry again (%d left) **\n\n",
			retry+1));
		dhd_bus_unreg_sdio_notify();
#if defined(CONFIG_WIFI_CONTROL_FUNC)
		wl_android_wifictrl_func_del();
#endif 
		dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
	} while (retry-- > 0);

	if (!chip_up) {
		DHD_ERROR(("\nfailed to power up wifi chip, max retry reached, exits **\n\n"));
		return -ENODEV;
	}
#else
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_ON);
#if defined(CONFIG_WIFI_CONTROL_FUNC)
	if (wl_android_wifictrl_func_add() < 0)
		goto fail_1;
#endif 

#endif 

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	sema_init(&dhd_registration_sem, 0);
#endif 

	error = dhd_bus_register();

	if (!error)
		printf("\n%s\n", dhd_version);
	else {
		DHD_ERROR(("%s: sdio_register_driver failed\n", __FUNCTION__));
		goto fail_1;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if ((down_timeout(&dhd_registration_sem,
		msecs_to_jiffies(DHD_REGISTRATION_TIMEOUT)) != 0) ||
		(dhd_registration_check != TRUE)) {
		error = -ENODEV;
		DHD_ERROR(("%s: sdio_register_driver timeout or error \n", __FUNCTION__));
		goto fail_2;
	}
#endif 
	if (wifi_fail_retry) {
		wifi_fail_retry = false;
		DHD_ERROR(("%s: wifi_fail_retry is true\n", __FUNCTION__));
		goto fail_2;
	}
#if defined(WL_CFG80211)
	wl_android_post_init();
#endif 

	module_insert = 1;
	printf("[ATS][press_widget][launch]\n"); 

	return error;

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
fail_2:
	dhd_bus_unregister();
#endif 

fail_1:

#if defined(CONFIG_WIFI_CONTROL_FUNC)
	wl_android_wifictrl_func_del();
#endif 

	
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
	if (!retry) {
		printf("module init fail, try again!\n");
		retry = 1;
		goto init_retry;
	}
	return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
late_initcall(dhd_module_init);
#else
module_init(dhd_module_init);
#endif

module_exit(dhd_module_cleanup);

int
dhd_os_proto_block(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		down(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

int
dhd_os_proto_unblock(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		up(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

unsigned int
dhd_os_get_ioctl_resp_timeout(void)
{
	return ((unsigned int)dhd_ioctl_timeout_msec);
}

void
dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec)
{
	dhd_ioctl_timeout_msec = (int)timeout_msec;
}

int
dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition, bool *pending)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);
#else
	timeout = timeout * HZ / 1000;
#endif

#if 0
	
	add_wait_queue(&dhd->ioctl_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	smp_mb();
	while (!(*condition) && (!signal_pending(current) && timeout)) {
		timeout = schedule_timeout(timeout);
		smp_mb();
	}

	if (signal_pending(current))
		*pending = TRUE;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dhd->ioctl_resp_wait, &wait);
#else
	timeout = wait_event_timeout(dhd->ioctl_resp_wait, (*condition), timeout);
#endif

	return timeout;
}

int
dhd_os_ioctl_resp_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (waitqueue_active(&dhd->ioctl_resp_wait)) {
		wake_up(&dhd->ioctl_resp_wait);
	}

	return 0;
}

void
dhd_os_wd_timer(void *bus, uint wdtick)
{
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd)
		return;

	flags = dhd_os_spin_lock(pub);

	
	if (pub->busstate == DHD_BUS_DOWN) {
		dhd_os_spin_unlock(pub, flags);
		return;
	}

	
	if (!wdtick && dhd->wd_timer_valid == TRUE) {
		dhd->wd_timer_valid = FALSE;
		dhd_os_spin_unlock(pub, flags);
#ifdef DHDTHREAD
		del_timer_sync(&dhd->timer);
#else
		del_timer(&dhd->timer);
#endif 
		return;
	}

	if (wdtick) {
		dhd_watchdog_ms = (uint)wdtick;
		
		mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);
		dhd->wd_timer_valid = TRUE;
	}
	dhd_os_spin_unlock(pub, flags);
}

void *
dhd_os_open_image(char *filename)
{
	struct file *fp;

	fp = filp_open(filename, O_RDONLY, 0);
	 if (IS_ERR(fp))
		 fp = NULL;

	 return fp;
}

int
dhd_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

void
dhd_os_close_image(void *image)
{
	if (image)
		filp_close((struct file *)image, NULL);
}


void
dhd_os_sdlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

#ifdef DHDTHREAD
	if (dhd->threads_only)
		down(&dhd->sdsem);
	else
#endif 
	spin_lock_bh(&dhd->sdlock);
}

void
dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

#ifdef DHDTHREAD
	if (dhd->threads_only)
		up(&dhd->sdsem);
	else
#endif 
	spin_unlock_bh(&dhd->sdlock);
}

void
dhd_os_sdlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->txqlock);
}

void
dhd_os_sdunlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->txqlock);
}

void
dhd_os_sdlock_rxq(dhd_pub_t *pub)
{
}

void
dhd_os_sdunlock_rxq(dhd_pub_t *pub)
{
}

void
dhd_os_sdtxlock(dhd_pub_t *pub)
{
	dhd_os_sdlock(pub);
}

void
dhd_os_sdtxunlock(dhd_pub_t *pub)
{
	dhd_os_sdunlock(pub);
}

#if defined(DHD_USE_STATIC_BUF)
uint8* dhd_os_prealloc(void *osh, int section, uint size)
{
	return (uint8*)wl_android_prealloc(section, size);
}

void dhd_os_prefree(void *osh, void *addr, uint size)
{
}
#endif 

#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *
dhd_get_wireless_stats(struct net_device *dev)
{
	int res = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (!dhd->pub.up) {
		return NULL;
	}

	res = wl_iw_get_wireless_stats(dev, &dhd->iw.wstats);

	if (res == 0)
		return &dhd->iw.wstats;
	else
		return NULL;
}
#endif 

static int
dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
	wl_event_msg_t *event, void **data)
{
	int bcmerror = 0;
	ASSERT(dhd != NULL);

	bcmerror = wl_host_event(&dhd->pub, ifidx, pktdata, event, data);
	if (bcmerror != BCME_OK)
		return (bcmerror);

#if defined(CONFIG_WIRELESS_EXT)
	if (
#if defined(APSTA_CONCURRENT) && defined(SOFTAP)
		!ap_net_dev && 
#endif
		event->bsscfgidx == 0) {

		ASSERT(dhd->iflist[*ifidx] != NULL);
		ASSERT(dhd->iflist[*ifidx]->net != NULL);

		if (dhd->iflist[*ifidx]->net) {
			wl_iw_event(dhd->iflist[*ifidx]->net, event, *data);
		}
	}
#if defined(APSTA_CONCURRENT) && defined(SOFTAP)
	if ( dhd->iflist[*ifidx]->net && (dhd->iflist[*ifidx]->net == ap_net_dev)){
		wl_iw_event(dhd->iflist[*ifidx]->net, event, *data);
		printf("%s: don't route event to wl_cfg80211 if the net_dev is ap_net_dev\n", __FUNCTION__);
		return BCME_OK;
	}
#endif

#endif 

#ifdef WL_CFG80211
	if ((ntoh32(event->event_type) == WLC_E_IF) &&
		(((dhd_if_event_t *)*data)->action == WLC_E_IF_ADD))
		return (BCME_OK);
	if ((wl_cfg80211_is_progress_ifchange() ||
		wl_cfg80211_is_progress_ifadd()) && (*ifidx != 0)) {
		return (BCME_OK);
	}

	ASSERT(dhd->iflist[*ifidx] != NULL);
	ASSERT(dhd->iflist[*ifidx]->net != NULL);
	if (dhd->iflist[*ifidx]->event2cfg80211 && dhd->iflist[*ifidx]->net) {
		wl_cfg80211_event(dhd->iflist[*ifidx]->net, event, *data);
	}
#endif 

	return (bcmerror);
}

void
dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	switch (ntoh32(event->event_type)) {
#ifdef WLBTAMP
	
	case WLC_E_BTA_HCI_EVENT: {
		struct sk_buff *p, *skb;
		bcm_event_t *msg;
		wl_event_msg_t *p_bcm_event;
		char *ptr;
		uint32 len;
		uint32 pktlen;
		dhd_if_t *ifp;
		dhd_info_t *dhd;
		uchar *eth;
		int ifidx;

		len = ntoh32(event->datalen);
		pktlen = sizeof(bcm_event_t) + len + 2;
		dhd = dhdp->info;
		ifidx = dhd_ifname2idx(dhd, event->ifname);

		if ((p = PKTGET(dhdp->osh, pktlen, FALSE))) {
			ASSERT(ISALIGNED((uintptr)PKTDATA(dhdp->osh, p), sizeof(uint32)));

			msg = (bcm_event_t *) PKTDATA(dhdp->osh, p);

			bcopy(&dhdp->mac, &msg->eth.ether_dhost, ETHER_ADDR_LEN);
			bcopy(&dhdp->mac, &msg->eth.ether_shost, ETHER_ADDR_LEN);
			ETHER_TOGGLE_LOCALADDR(&msg->eth.ether_shost);

			msg->eth.ether_type = hton16(ETHER_TYPE_BRCM);

			
			msg->bcm_hdr.subtype = hton16(BCMILCP_SUBTYPE_VENDOR_LONG);
			msg->bcm_hdr.version = BCMILCP_BCM_SUBTYPEHDR_VERSION;
			bcopy(BRCM_OUI, &msg->bcm_hdr.oui[0], DOT11_OUI_LEN);

			msg->bcm_hdr.length = hton16(BCMILCP_BCM_SUBTYPEHDR_MINLENGTH +
				BCM_MSG_LEN + sizeof(wl_event_msg_t) + (uint16)len);
			msg->bcm_hdr.usr_subtype = hton16(BCMILCP_BCM_SUBTYPE_EVENT);

			PKTSETLEN(dhdp->osh, p, (sizeof(bcm_event_t) + len + 2));

			

			
			p_bcm_event = &msg->event;
			bcopy(event, p_bcm_event, sizeof(wl_event_msg_t));

			
			bcopy(data, (p_bcm_event + 1), len);

			msg->bcm_hdr.length  = hton16(sizeof(wl_event_msg_t) +
				ntoh16(msg->bcm_hdr.length));
			PKTSETLEN(dhdp->osh, p, (sizeof(bcm_event_t) + len + 2));

			ptr = (char *)(msg + 1);
			ptr[len+0] = 0x00;
			ptr[len+1] = 0x00;

			skb = PKTTONATIVE(dhdp->osh, p);
			eth = skb->data;
			len = skb->len;

			ifp = dhd->iflist[ifidx];
			if (ifp == NULL)
			     ifp = dhd->iflist[0];

			ASSERT(ifp);
			skb->dev = ifp->net;
			skb->protocol = eth_type_trans(skb, skb->dev);

			skb->data = eth;
			skb->len = len;

			
			skb_pull(skb, ETH_HLEN);

			
			if (in_interrupt()) {
				netif_rx(skb);
			} else {
				netif_rx_ni(skb);
			}
		}
		else {
			
			DHD_ERROR(("%s: unable to alloc sk_buf", __FUNCTION__));
		}
		break;
	} 
#endif 

	default:
		break;
	}
}

void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar)
{
#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	int timeout = msecs_to_jiffies(2000);
#else
	int timeout = 2 * HZ;
#endif 
	dhd_os_sdunlock(dhd);
	wait_event_timeout(dhdinfo->ctrl_wait, (*lockvar == FALSE), timeout);
	dhd_os_sdlock(dhd);
#endif
	return;
}

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
	if (waitqueue_active(&dhdinfo->ctrl_wait))
		wake_up(&dhdinfo->ctrl_wait);
#endif
	return;
}

int
dhd_dev_reset(struct net_device *dev, uint8 flag)
{
	int ret;

	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	ret = dhd_bus_devreset(&dhd->pub, flag);
	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
		return ret;
	}

	return ret;
}

int net_os_set_suspend_disable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd) {
		ret = dhd->pub.suspend_disable_flag;
		dhd->pub.suspend_disable_flag = val;
	}
	return ret;
}

int net_os_set_suspend(struct net_device *dev, int val, int force)
{
	int ret = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd) {
#if defined(CONFIG_HAS_EARLYSUSPEND)
		ret = dhd_set_suspend(val, &dhd->pub);
#else
		ret = dhd_suspend_resume_helper(dhd, val, force);
#endif
	}

	return ret;
}

int net_os_set_dtim_skip(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd)
		dhd->pub.dtim_skip = val;

	return 0;
}

#ifdef PKT_FILTER_SUPPORT
int net_os_rxfilter_add_remove(struct net_device *dev, int add_remove, int num)
{
#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	char *filterp = NULL;
	int ret = 0;

	if (!dhd || (num == DHD_UNICAST_FILTER_NUM) ||
	    (num == DHD_MDNS_FILTER_NUM))
		return ret;
	if (num >= dhd->pub.pktfilter_count)
		return -EINVAL;
	if (add_remove) {
		switch (num) {
		case DHD_BROADCAST_FILTER_NUM:
			filterp = "101 0 0 0 0xFFFFFFFFFFFF 0xFFFFFFFFFFFF";
			break;
		case DHD_MULTICAST4_FILTER_NUM:
			filterp = "102 0 0 0 0xFFFFFF 0x01005E";
			break;
		case DHD_MULTICAST6_FILTER_NUM:
#if defined(BLOCK_IPV6_PACKET)
			return ret;
#endif
			filterp = "103 0 0 0 0xFFFF 0x3333";
			break;
		default:
			return -EINVAL;
		}
	}
	dhd->pub.pktfilter[num] = filterp;
	return ret;
#else
	return 0;
#endif 
}
#endif

int wl_android_set_pktfilter(struct net_device *dev, struct dd_pkt_filter_s *data)
{
    dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
    return dhd_set_pktfilter(&dhd->pub, data->add, data->id, data->offset, data->mask, data->pattern);

}

int net_os_set_packet_filter(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd && dhd->pub.up) {
		if (dhd->pub.in_suspend) {
			if (!val || (val && !dhd->pub.suspend_disable_flag))
				dhd_set_packet_filter(val, &dhd->pub);
		}
	}
	return ret;
}


void
dhd_dev_init_ioctl(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	dhd_preinit_ioctls(&dhd->pub);
}

#ifdef PNO_SUPPORT
int
dhd_dev_pno_reset(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_clean(&dhd->pub));
}


int
dhd_dev_pno_enable(struct net_device *dev,  int pfn_enabled)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_enable(&dhd->pub, pfn_enabled));
}


int
dhd_dev_pno_set(struct net_device *dev, wlc_ssid_t* ssids_local, int nssid,
	ushort  scan_fr, int pno_repeat, int pno_freq_expo_max)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_set(&dhd->pub, ssids_local, nssid, scan_fr, pno_repeat, pno_freq_expo_max));
}

int
dhd_dev_get_pno_status(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_get_status(&dhd->pub));
}

#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (1)
static void dhd_hang_process(struct work_struct *work)
{
	dhd_info_t *dhd;
	struct net_device *dev;
	int ret;
	s32 updown = 0;

	dhd = (dhd_info_t *)container_of(work, dhd_info_t, work_hang);
	dev = dhd->iflist[0]->net;

	if (dev) {
		
		
		
		printf(" %s before send hang messages, do wlc down to prevent get additional event from firmware\n",__FUNCTION__);
		if ((ret = wldev_ioctl(dev, WLC_DOWN, &updown, sizeof(s32), false))) {
			WL_ERR(("fail to set wlc down"));
		}
#if defined(WL_WIRELESS_EXT)
		wl_iw_send_priv_event(dev, "HANG");
#endif
#if defined(WL_CFG80211)
		wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
	}
}

int dhd_os_send_hang_message(dhd_pub_t *dhdp)
{
	int ret = 0;
	if (dhdp) {
		if (!dhdp->hang_was_sent) {
			dhdp->hang_was_sent = 1;
			printf("%s: schedule hang event\n", __FUNCTION__);
			schedule_work(&dhdp->info->work_hang);
		}
	}
	return ret;
}

int net_os_send_hang_message(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (module_remove) {
		printf("%s: module removed. Do not send hang event.\n", __FUNCTION__);
		return ret;
	}

	if (dhd)
		ret = dhd_os_send_hang_message(&dhd->pub);

	return ret;
}
#endif 

bool check_hang_already(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd->pub.hang_was_sent)
		return TRUE;
	else
		return FALSE;
}

#if defined(CONFIG_WIRELESS_EXT)
void dhd_info_send_hang_message(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct net_device *dev = NULL;
	if ((dhd == NULL) || dhd->iflist[0]->net == NULL) {
		return;
	}

	dev = dhd->iflist[0]->net;
	net_os_send_hang_message(dev);

	return;
}

int net_os_send_rssilow_message(struct net_device *dev)
{
        dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
        int ret = 0;

        if (module_remove) {
                printf("%s: module removed. Do not send rssi_low event.\n", __FUNCTION__);
                return ret;
        }

        if (dhd) {
               
			   ret = wl_cfg80211_rssilow(dev);
        }
        return ret;
}
#endif

void dhd_bus_country_set(struct net_device *dev, wl_country_t *cspec)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd && dhd->pub.up)
			memcpy(&dhd->pub.dhd_cspec, cspec, sizeof(wl_country_t));
}

wl_country_t *dhd_bus_country_get(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd && (dhd->pub.dhd_cspec.ccode[0] != 0))
		return &dhd->pub.dhd_cspec;
	return NULL;
}

void dhd_net_if_lock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_net_if_lock_local(dhd);
}

void dhd_net_if_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_net_if_unlock_local(dhd);
}

static void dhd_net_if_lock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (dhd)
		mutex_lock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_net_if_unlock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (dhd)
		mutex_unlock(&dhd->dhd_net_if_mutex);
#endif
}

#if 0
static void dhd_suspend_lock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_lock(&dhd->dhd_suspend_mutex);
#endif
}

static void dhd_suspend_unlock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_unlock(&dhd->dhd_suspend_mutex);
#endif
}
#endif

unsigned long dhd_os_spin_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags = 0;

	if (dhd)
		spin_lock_irqsave(&dhd->dhd_lock, flags);

	return flags;
}

void dhd_os_spin_unlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		spin_unlock_irqrestore(&dhd->dhd_lock, flags);
}

static int
dhd_get_pend_8021x_cnt(dhd_info_t *dhd)
{
	return (atomic_read(&dhd->pend_8021x_cnt));
}

#define MAX_WAIT_FOR_8021X_TX	25

int
dhd_wait_pend8021x(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int timeout = 10 * HZ / 1000;
	int ntimes = MAX_WAIT_FOR_8021X_TX;
	int pend = dhd_get_pend_8021x_cnt(dhd);

	while (ntimes && pend) {
		if (pend) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(timeout);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = dhd_get_pend_8021x_cnt(dhd);
	}
	if (ntimes == 0)
		DHD_ERROR(("%s: TIMEOUT\n", __FUNCTION__));
	return pend;
}

#ifdef DHD_DEBUG
int
write_to_file(dhd_pub_t *dhd, uint8 *buf, int size)
{
	int ret = 0;
	struct file *fp;
	mm_segment_t old_fs;
	loff_t pos = 0;

	
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	
	fp = filp_open("/tmp/mem_dump", O_WRONLY|O_CREAT, 0640);
	if (!fp) {
		printf("%s: open file error\n", __FUNCTION__);
		ret = -1;
		goto exit;
	}

	
	fp->f_op->write(fp, buf, size, &pos);

exit:
	
	MFREE(dhd->osh, buf, size);
	
	if (fp)
		filp_close(fp, current->files);
	
	set_fs(old_fs);

	return ret;
}
#endif 

void dhd_htc_wake_lock_timeout(dhd_pub_t *pub, int sec)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_timeout(&dhd->wl_htc, sec * HZ);
#endif
}

int dhd_os_wake_lock_timeout(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		ret = dhd->wakelock_rx_timeout_enable > dhd->wakelock_ctrl_timeout_enable ?
			dhd->wakelock_rx_timeout_enable : dhd->wakelock_ctrl_timeout_enable;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd->wakelock_rx_timeout_enable)
			wake_lock_timeout(&dhd->wl_rxwake,
				msecs_to_jiffies(dhd->wakelock_rx_timeout_enable));
		if (dhd->wakelock_ctrl_timeout_enable)
			wake_lock_timeout(&dhd->wl_ctrlwake,
				msecs_to_jiffies(dhd->wakelock_ctrl_timeout_enable));
#endif
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_lock_timeout(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_timeout(&dhd->pub);
	return ret;
}

int dhd_os_wake_lock_rx_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_rx_timeout_enable)
			dhd->wakelock_rx_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_ctrl_timeout_enable)
			dhd->wakelock_ctrl_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int net_os_wake_lock_rx_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_rx_timeout_enable(&dhd->pub, val);
	return ret;
}

int net_os_wake_lock_ctrl_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_ctrl_timeout_enable(&dhd->pub, val);
	return ret;
}

int dhd_os_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
#ifdef CONFIG_HAS_WAKELOCK
		if (!dhd->wakelock_counter)
			wake_lock(&dhd->wl_wifi);
#elif defined(CUSTOMER_HW4) && defined(CONFIG_PM_SLEEP) && defined(PLATFORM_SLP)
		
		pm_stay_awake(pm_dev);
#endif 
		dhd->wakelock_counter++;
		ret = dhd->wakelock_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_lock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock(&dhd->pub);
	return ret;
}

int dhd_os_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	dhd_os_wake_lock_timeout(pub);
	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_counter) {
			dhd->wakelock_counter--;
#ifdef CONFIG_HAS_WAKELOCK
			if (!dhd->wakelock_counter)
				wake_unlock(&dhd->wl_wifi);
#elif defined(CUSTOMER_HW4) && defined(CONFIG_PM_SLEEP) && defined(PLATFORM_SLP)
			
			pm_relax(pm_dev);
#endif 
			ret = dhd->wakelock_counter;
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_unlock(&dhd->pub);
	return ret;
}

int dhd_os_wake_force_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	dhd_os_wake_lock_timeout(pub);
	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_counter) {
			printf("wakelock_counter = %d, set to 0\n", dhd->wakelock_counter);
			dhd->wakelock_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
			if (!dhd->wakelock_counter)
				wake_unlock(&dhd->wl_wifi);
#endif
			ret = dhd->wakelock_counter;
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_check_wakelock(void *dhdp)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_pub_t *pub = (dhd_pub_t *)dhdp;
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);

	if (dhd && wake_lock_active(&dhd->wl_wifi))
		return 1;
#elif defined(CUSTOMER_HW4) && defined(CONFIG_PM_SLEEP) && defined(PLATFORM_SLP)
	
	dhd_pub_t *pub = (dhd_pub_t *)dhdp;
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);

	DHD_ERROR(("%s : wakelock_count = %d\n", __func__, dhd->wakelock_counter));

	if (dhd && (dhd->wakelock_counter > 0))
		return 1;
#endif 
	return 0;
}

int dhd_os_check_if_up(void *dhdp)
{
	dhd_pub_t *pub = (dhd_pub_t *)dhdp;

	if (!pub)
		return 0;
	return pub->up;
}
int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd)
{
	int ifidx;
	int ret = 0;
	dhd_info_t *dhd = NULL;

	if (!net || !netdev_priv(net)) {
		DHD_ERROR(("%s invalid parameter\n", __FUNCTION__));
		return -EINVAL;
	}

	dhd = *(dhd_info_t **)netdev_priv(net);
	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);
	ret = dhd_wl_ioctl(&dhd->pub, ifidx, ioc, ioc->buf, ioc->len);
	dhd_check_hang(net, &dhd->pub, ret);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return ret;
}

bool dhd_os_check_hang(dhd_pub_t *dhdp, int ifidx, int ret)
{
	struct net_device *net;

	net = dhd_idx2net(dhdp, ifidx);
	return dhd_check_hang(net, dhdp, ret);
}
#if defined(WL_CFG80211) && defined(SUPPORT_DEEP_SLEEP)
#define MAX_TRY_CNT		5 
int dhd_deepsleep(struct net_device *dev, int flag)
{
	char iovbuf[20];
	uint powervar = 0;
	dhd_info_t *dhd;
	dhd_pub_t *dhdp;
	int cnt = 0;
	int ret = 0;

	dhd = *(dhd_info_t **)netdev_priv(dev);
	dhdp = &dhd->pub;

	switch (flag) {
	case 1 :  
		DHD_ERROR(("[WiFi] Deepsleep On\n"));
		
		msleep(200);
#ifdef PKT_FILTER_SUPPORT
		
		dhd_set_packet_filter(0, dhdp);
#endif 
		
		powervar = 0;
		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("mpc", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

		
		powervar = 1;
		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("deepsleep", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
		break;

	case 0: 
		DHD_ERROR(("[WiFi] Deepsleep Off\n"));

		
		for (cnt = 0; cnt < MAX_TRY_CNT; cnt++) {
			powervar = 0;
			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("deepsleep", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("deepsleep", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhdp, WLC_GET_VAR, iovbuf, sizeof(iovbuf),
				FALSE, 0)) < 0) {
				DHD_ERROR(("the error of dhd deepsleep status ret value :%d\n",
					ret));
			} else {
				if (!(*(int *)iovbuf)) {
					DHD_ERROR(("deepsleep mode is 0, ok , count : %d\n", cnt));
					break;
				}
			}
		}

		
		powervar = 1;
		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("mpc", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
		break;
	}

	return 0;
}
#endif 

#ifdef PROP_TXSTATUS
extern int dhd_wlfc_interface_entry_update(void* state,	ewlfc_mac_entry_action_t action, uint8 ifid,
	uint8 iftype, uint8* ea);
extern int dhd_wlfc_FIFOcreditmap_update(void* state, uint8* credits);

int dhd_wlfc_interface_event(struct dhd_info *dhd,
	ewlfc_mac_entry_action_t action, uint8 ifid, uint8 iftype, uint8* ea)
{
	if (dhd->pub.wlfc_state == NULL)
		return BCME_OK;

	return dhd_wlfc_interface_entry_update(dhd->pub.wlfc_state, action, ifid, iftype, ea);
}

int dhd_wlfc_FIFOcreditmap_event(struct dhd_info *dhd, uint8* event_data)
{
	if (dhd->pub.wlfc_state == NULL)
		return BCME_OK;

	return dhd_wlfc_FIFOcreditmap_update(dhd->pub.wlfc_state, event_data);
}

int dhd_wlfc_event(struct dhd_info *dhd)
{
	return dhd_wlfc_enable(&dhd->pub);
}
#endif 

#ifdef BCMDBGFS

#include <linux/debugfs.h>

extern uint32 dhd_readregl(void *bp, uint32 addr);
extern uint32 dhd_writeregl(void *bp, uint32 addr, uint32 data);

typedef struct dhd_dbgfs {
	struct dentry	*debugfs_dir;
	struct dentry	*debugfs_mem;
	dhd_pub_t 	*dhdp;
	uint32 		size;
} dhd_dbgfs_t;

dhd_dbgfs_t g_dbgfs;

static int
dhd_dbg_state_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t
dhd_dbg_state_read(struct file *file, char __user *ubuf,
                       size_t count, loff_t *ppos)
{
	ssize_t rval;
	uint32 tmp;
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	
	tmp = dhd_readregl(g_dbgfs.dhdp->bus, file->f_pos & (~3));

	ret = copy_to_user(ubuf, &tmp, 4);
	if (ret == count)
		return -EFAULT;

	count -= ret;
	*ppos = pos + count;
	rval = count;

	return rval;
}


static ssize_t
dhd_debugfs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	size_t ret;
	uint32 buf;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	ret = copy_from_user(&buf, ubuf, sizeof(uint32));
	if (ret == count)
		return -EFAULT;

	
	dhd_writeregl(g_dbgfs.dhdp->bus, file->f_pos & (~3), buf);

	return count;
}


loff_t
dhd_debugfs_lseek(struct file *file, loff_t off, int whence)
{
	loff_t pos = -1;

	switch (whence) {
		case 0:
			pos = off;
			break;
		case 1:
			pos = file->f_pos + off;
			break;
		case 2:
			pos = g_dbgfs.size - off;
	}
	return (pos < 0 || pos > g_dbgfs.size) ? -EINVAL : (file->f_pos = pos);
}

static const struct file_operations dhd_dbg_state_ops = {
	.read   = dhd_dbg_state_read,
	.write	= dhd_debugfs_write,
	.open   = dhd_dbg_state_open,
	.llseek	= dhd_debugfs_lseek
};

static void dhd_dbg_create(void)
{
	if (g_dbgfs.debugfs_dir) {
		g_dbgfs.debugfs_mem = debugfs_create_file("mem", 0644, g_dbgfs.debugfs_dir,
			NULL, &dhd_dbg_state_ops);
	}
}

void dhd_dbg_init(dhd_pub_t *dhdp)
{
	int err;

	g_dbgfs.dhdp = dhdp;
	g_dbgfs.size = 0x20000000; 

	g_dbgfs.debugfs_dir = debugfs_create_dir("dhd", 0);
	if (IS_ERR(g_dbgfs.debugfs_dir)) {
		err = PTR_ERR(g_dbgfs.debugfs_dir);
		g_dbgfs.debugfs_dir = NULL;
		return;
	}

	dhd_dbg_create();

	return;
}

void dhd_dbg_remove(void)
{
	debugfs_remove(g_dbgfs.debugfs_mem);
	debugfs_remove(g_dbgfs.debugfs_dir);

	bzero((unsigned char *) &g_dbgfs, sizeof(g_dbgfs));

}
#endif 

#ifdef WLMEDIA_HTSF

static
void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct sk_buff *skb;
	uint32 htsf = 0;
	uint16 dport = 0, oldmagic = 0xACAC;
	char *p1;
	htsfts_t ts;

	

	p1 = (char*) PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(dhdp->osh, pktbuf) > HTSF_MINLEN) {
		memcpy(&dport, p1+40, 2);
		dport = ntoh16(dport);
	}

	
	if (dport >= tsport && dport <= tsport + 20) {

		skb = (struct sk_buff *) pktbuf;

		htsf = dhd_get_htsf(dhd, 0);
		memset(skb->data + 44, 0, 2); 
		memcpy(skb->data+82, &oldmagic, 2);
		memcpy(skb->data+84, &htsf, 4);

		memset(&ts, 0, sizeof(htsfts_t));
		ts.magic  = HTSFMAGIC;
		ts.prio   = PKTPRIO(pktbuf);
		ts.seqnum = htsf_seqnum++;
		ts.c10    = get_cycles();
		ts.t10    = htsf;
		ts.endmagic = HTSFENDMAGIC;

		memcpy(skb->data + HTSF_HOSTOFFSET, &ts, sizeof(ts));
	}
}

static void dhd_dump_htsfhisto(histo_t *his, char *s)
{
	int pktcnt = 0, curval = 0, i;
	for (i = 0; i < (NUMBIN-2); i++) {
		curval += 500;
		printf("%d ",  his->bin[i]);
		pktcnt += his->bin[i];
	}
	printf(" max: %d TotPkt: %d neg: %d [%s]\n", his->bin[NUMBIN-2], pktcnt,
		his->bin[NUMBIN-1], s);
}

static
void sorttobin(int value, histo_t *histo)
{
	int i, binval = 0;

	if (value < 0) {
		histo->bin[NUMBIN-1]++;
		return;
	}
	if (value > histo->bin[NUMBIN-2])  
		histo->bin[NUMBIN-2] = value;

	for (i = 0; i < (NUMBIN-2); i++) {
		binval += 500; 
		if (value <= binval) {
			histo->bin[i]++;
			return;
		}
	}
	histo->bin[NUMBIN-3]++;
}

static
void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	char *p1;
	uint16 old_magic;
	int d1, d2, d3, end2end;
	htsfts_t *htsf_ts;
	uint32 htsf;

	skb = PKTTONATIVE(dhdp->osh, pktbuf);
	p1 = (char*)PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(osh, pktbuf) > HTSF_MINLEN) {
		memcpy(&old_magic, p1+78, 2);
		htsf_ts = (htsfts_t*) (p1 + HTSF_HOSTOFFSET - 4);
	}
	else
		return;

	if (htsf_ts->magic == HTSFMAGIC) {
		htsf_ts->tE0 = dhd_get_htsf(dhd, 0);
		htsf_ts->cE0 = get_cycles();
	}

	if (old_magic == 0xACAC) {

		tspktcnt++;
		htsf = dhd_get_htsf(dhd, 0);
		memcpy(skb->data+92, &htsf, sizeof(uint32));

		memcpy(&ts[tsidx].t1, skb->data+80, 16);

		d1 = ts[tsidx].t2 - ts[tsidx].t1;
		d2 = ts[tsidx].t3 - ts[tsidx].t2;
		d3 = ts[tsidx].t4 - ts[tsidx].t3;
		end2end = ts[tsidx].t4 - ts[tsidx].t1;

		sorttobin(d1, &vi_d1);
		sorttobin(d2, &vi_d2);
		sorttobin(d3, &vi_d3);
		sorttobin(end2end, &vi_d4);

		if (end2end > 0 && end2end >  maxdelay) {
			maxdelay = end2end;
			maxdelaypktno = tspktcnt;
			memcpy(&maxdelayts, &ts[tsidx], 16);
		}
		if (++tsidx >= TSMAX)
			tsidx = 0;
	}
}

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx)
{
	uint32 htsf = 0, cur_cycle, delta, delta_us;
	uint32    factor, baseval, baseval2;
	cycles_t t;

	t = get_cycles();
	cur_cycle = t;

	if (cur_cycle >  dhd->htsf.last_cycle)
		delta = cur_cycle -  dhd->htsf.last_cycle;
	else {
		delta = cur_cycle + (0xFFFFFFFF -  dhd->htsf.last_cycle);
	}

	delta = delta >> 4;

	if (dhd->htsf.coef) {
		
	        factor = (dhd->htsf.coef*10 + dhd->htsf.coefdec1);
		baseval  = (delta*10)/factor;
		baseval2 = (delta*10)/(factor+1);
		delta_us  = (baseval -  (((baseval - baseval2) * dhd->htsf.coefdec2)) / 10);
		htsf = (delta_us << 4) +  dhd->htsf.last_tsf + HTSF_BUS_DELAY;
	}
	else {
		DHD_ERROR(("-------dhd->htsf.coef = 0 -------\n"));
	}

	return htsf;
}

static void dhd_dump_latency(void)
{
	int i, max = 0;
	int d1, d2, d3, d4, d5;

	printf("T1       T2       T3       T4           d1  d2   t4-t1     i    \n");
	for (i = 0; i < TSMAX; i++) {
		d1 = ts[i].t2 - ts[i].t1;
		d2 = ts[i].t3 - ts[i].t2;
		d3 = ts[i].t4 - ts[i].t3;
		d4 = ts[i].t4 - ts[i].t1;
		d5 = ts[max].t4-ts[max].t1;
		if (d4 > d5 && d4 > 0)  {
			max = i;
		}
		printf("%08X %08X %08X %08X \t%d %d %d   %d i=%d\n",
			ts[i].t1, ts[i].t2, ts[i].t3, ts[i].t4,
			d1, d2, d3, d4, i);
	}

	printf("current idx = %d \n", tsidx);

	printf("Highest latency %d pkt no.%d total=%d\n", maxdelay, maxdelaypktno, tspktcnt);
	printf("%08X %08X %08X %08X \t%d %d %d   %d\n",
	maxdelayts.t1, maxdelayts.t2, maxdelayts.t3, maxdelayts.t4,
	maxdelayts.t2 - maxdelayts.t1,
	maxdelayts.t3 - maxdelayts.t2,
	maxdelayts.t4 - maxdelayts.t3,
	maxdelayts.t4 - maxdelayts.t1);
}


static int
dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;
	uint32 s1, s2;

	struct tsf {
		uint32 low;
		uint32 high;
	} tsf_buf;

	memset(&ioc, 0, sizeof(ioc));
	memset(&tsf_buf, 0, sizeof(tsf_buf));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = FALSE;

	strncpy(buf, "tsf", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	s1 = dhd_get_htsf(dhd, 0);
	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		if (ret == -EIO) {
			DHD_ERROR(("%s: tsf is not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}
		return ret;
	}
	s2 = dhd_get_htsf(dhd, 0);

	memcpy(&tsf_buf, buf, sizeof(tsf_buf));
	printf(" TSF_h=%04X lo=%08X Calc:htsf=%08X, coef=%d.%d%d delta=%d ",
		tsf_buf.high, tsf_buf.low, s2, dhd->htsf.coef, dhd->htsf.coefdec1,
		dhd->htsf.coefdec2, s2-tsf_buf.low);
	printf("lasttsf=%08X lastcycle=%08X\n", dhd->htsf.last_tsf, dhd->htsf.last_cycle);
	return 0;
}

void htsf_update(dhd_info_t *dhd, void *data)
{
	static ulong  cur_cycle = 0, prev_cycle = 0;
	uint32 htsf, tsf_delta = 0;
	uint32 hfactor = 0, cyc_delta, dec1 = 0, dec2, dec3, tmp;
	ulong b, a;
	cycles_t t;

	

	t = get_cycles();

	prev_cycle = cur_cycle;
	cur_cycle = t;

	if (cur_cycle > prev_cycle)
		cyc_delta = cur_cycle - prev_cycle;
	else {
		b = cur_cycle;
		a = prev_cycle;
		cyc_delta = cur_cycle + (0xFFFFFFFF - prev_cycle);
	}

	if (data == NULL)
		printf(" tsf update ata point er is null \n");

	memcpy(&prev_tsf, &cur_tsf, sizeof(tsf_t));
	memcpy(&cur_tsf, data, sizeof(tsf_t));

	if (cur_tsf.low == 0) {
		DHD_INFO((" ---- 0 TSF, do not update, return\n"));
		return;
	}

	if (cur_tsf.low > prev_tsf.low)
		tsf_delta = (cur_tsf.low - prev_tsf.low);
	else {
		DHD_INFO((" ---- tsf low is smaller cur_tsf= %08X, prev_tsf=%08X, \n",
		 cur_tsf.low, prev_tsf.low));
		if (cur_tsf.high > prev_tsf.high) {
			tsf_delta = cur_tsf.low + (0xFFFFFFFF - prev_tsf.low);
			DHD_INFO((" ---- Wrap around tsf coutner  adjusted TSF=%08X\n", tsf_delta));
		}
		else
			return; 
	}

	if (tsf_delta)  {
		hfactor = cyc_delta / tsf_delta;
		tmp  = 	(cyc_delta - (hfactor * tsf_delta))*10;
		dec1 =  tmp/tsf_delta;
		dec2 =  ((tmp - dec1*tsf_delta)*10) / tsf_delta;
		tmp  = 	(tmp   - (dec1*tsf_delta))*10;
		dec3 =  ((tmp - dec2*tsf_delta)*10) / tsf_delta;

		if (dec3 > 4) {
			if (dec2 == 9) {
				dec2 = 0;
				if (dec1 == 9) {
					dec1 = 0;
					hfactor++;
				}
				else {
					dec1++;
				}
			}
			else
				dec2++;
		}
	}

	if (hfactor) {
		htsf = ((cyc_delta * 10)  / (hfactor*10+dec1)) + prev_tsf.low;
		dhd->htsf.coef = hfactor;
		dhd->htsf.last_cycle = cur_cycle;
		dhd->htsf.last_tsf = cur_tsf.low;
		dhd->htsf.coefdec1 = dec1;
		dhd->htsf.coefdec2 = dec2;
	}
	else {
		htsf = prev_tsf.low;
	}
}

#endif 

void dhd_reset_hang_was_sent(struct net_device *dev)
{
    dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
    if (dhd) 
        dhd->pub.hang_was_sent = 0;
    
}

int dhd_get_txrx_stats(struct net_device *net, unsigned long *rx_packets, unsigned long *tx_packets)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_pub_t *dhdp;

	if (!dhd)
		return -1;

	dhdp = &dhd->pub;

	if (!dhdp)
		return -1;

	*rx_packets = dhdp->rx_packets;
	*tx_packets = dhdp->tx_packets;

	return 0;
}
