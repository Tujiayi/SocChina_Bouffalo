/* All Include Files */
#include <sys/errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include "bluetooth.h"
#include "conn.h"
#include "conn_internal.h"
#include "gatt.h"
#include "hci_core.h"
#include "uuid.h"
#include "btble.h"
#include "bt_log.h"
#include "bflb_gpio.h"
/* All Wireless Define */
uint8_t Ble_Buffer[255] = {0};
uint8_t *Location_1 ;
uint8_t *Location_2 ;
uint8_t *Location_3 ;
uint8_t len;
extern uint8_t *SSID_Wifi[30];
extern uint8_t *PASSID_Wifi[30];
/* The Flag For the WIFI to Connect */
extern uint32_t wifi_strat_flag;

#define TP_PRIO configMAX_PRIORITIES - 5
static void ble_tp_connected(struct bt_conn *conn, u8_t err);
static void ble_tp_disconnected(struct bt_conn *conn, u8_t reason);
static int bl_tp_send_indicate(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *data, u16_t len);
struct bt_conn *ble_tp_conn;
struct bt_gatt_exchange_params exchg_mtu;
TaskHandle_t ble_tp_task_h;
int tx_mtu_size = 20;
/* Enable the task. */
u8_t tp_start = 1;
static u8_t created_tp_task = 0;
static u8_t isRegister = 0;
static struct bt_conn_cb ble_tp_conn_callbacks = {
	.connected	=   ble_tp_connected,
	.disconnected	=   ble_tp_disconnected,
};

/*************************************************************************
NAME
    ble_tp_tx_mtu_size
*/
static void ble_tp_tx_mtu_size(struct bt_conn *conn, u8_t err,
			  struct bt_gatt_exchange_params *params)
{
   if(!err)
   {
        tx_mtu_size = bt_gatt_get_mtu(ble_tp_conn);
        BT_WARN("ble tp echange mtu size success, mtu size: %d", tx_mtu_size);
   }
   else
   {
        BT_WARN("ble tp echange mtu size failure, err: %d", err);
   }
}

/*************************************************************************
NAME
    ble_tp_connected
*/
static void ble_tp_connected(struct bt_conn *conn, u8_t err)
{
    if(err || conn->type != BT_CONN_TYPE_LE)
    {
        return;
    }

    int tx_octets = 0x00fb;
    int tx_time = 0x0848;
    int ret = -1;

    BT_INFO("%s",__func__);
    ble_tp_conn = conn;

    //set data length after connected.
    ret = bt_le_set_data_len(ble_tp_conn, tx_octets, tx_time);
    if(!ret)
    {
        BT_WARN("ble tp set data length success.");
    }
    else
    {
        BT_WARN("ble tp set data length failure, err: %d\n", ret);
    }

    //exchange mtu size after connected.
    exchg_mtu.func = ble_tp_tx_mtu_size;
    ret = bt_gatt_exchange_mtu(ble_tp_conn, &exchg_mtu);
    if (!ret) {
        BT_WARN("ble tp exchange mtu size pending.");
    } else {
        BT_WARN("ble tp exchange mtu size failure, err: %d", ret);
    }
}

/*************************************************************************
NAME    
    ble_tp_disconnected
*/
static void ble_tp_disconnected(struct bt_conn *conn, u8_t reason)
{ 
    if(conn->type != BT_CONN_TYPE_LE)
    {
        return;
    }

    BT_INFO("%s",__func__);

    ble_tp_conn = NULL;
}

/*************************************************************************
NAME
    ble_tp_recv_rd
*/
static int ble_tp_recv_rd(struct bt_conn *conn,	const struct bt_gatt_attr *attr,
                                        void *buf, u16_t len, u16_t offset)
{
    int size = 9;
    char data[9] = {0x11, 0x12, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};

    memcpy(buf, data, size);

    return size;
}

/*************************************************************************
NAME
    ble_tp_recv_wr
    In this system design, Bluetooth configuration network only needs to use this function function.
*/
static int ble_tp_recv_wr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                        const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    BT_WARN("recv data len=%d, offset=%d, flag=%d\r\n", len, offset, flags);
    strcpy(Ble_Buffer, buf);
    BT_WARN("recv copy data:%s\r\n", Ble_Buffer);
    if(strstr(Ble_Buffer,"WIFI_PASSID")){
        Location_1 = strstr(Ble_Buffer, "WIFI_PASSID");
        printf("%s\r\n", Location_1);
        Location_2 = strstr(Ble_Buffer, "WIFI_SSID");
        Location_3 = strstr(Ble_Buffer, "###");
        printf("%s\r\n", Location_2);
        len = Location_2 - Location_1 - 11;
        memcpy(PASSID_Wifi, Location_1 + 11, len);
        printf("***************%d\r\n", (uint16_t)len);
        len = Location_3 - Location_2 - 9;
        memcpy(SSID_Wifi, Location_2 + 9, len);
        printf("***************%d\r\n", (uint16_t)len);
        printf("%s\r\n", PASSID_Wifi);
        printf("%s\r\n", SSID_Wifi);
        BT_WARN("CONECTION OK\r\n");
        wifi_strat_flag = 0;
    }
    else
    {
        BT_WARN("CONECTION NOT OK\r\n");
    }

    if (flags & BT_GATT_WRITE_FLAG_PREPARE)
    {
        //Don't use prepare write data, execute write will upload data again.
        BT_WARN("rcv prepare write request");
        return 0;
    }

    if(flags & BT_GATT_WRITE_FLAG_CMD)
    {
        //Use write command data.
        BT_WARN("rcv write command");
    }
    else
    {
        //Use write request / execute write data.
        BT_WARN("rcv write request / exce write");
    }
    // k_sem_give(&notify_poll_sem);
    return len;
}

/*************************************************************************
NAME    
    indicate_rsp /bl_tp_send_indicate
*/ 
void indicate_rsp(struct bt_conn *conn, const struct bt_gatt_attr *attr,	u8_t err)
{
    BT_WARN("receive confirm, err:%d", err);
    struct bt_gatt_indicate_params *ind_params = (struct bt_gatt_indicate_params *)bt_att_get_att_req(conn);
    if(ind_params)
    {
        k_free(ind_params);
    }
}

/*************************************************************************
NAME
    ble_tp_ind_ccc_changed
*/
static void ble_tp_ind_ccc_changed(const struct bt_gatt_attr *attr, u16_t value)
{
    int err = -1;
    char data[9] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};

    if(value == BT_GATT_CCC_INDICATE) {
        err = bl_tp_send_indicate(ble_tp_conn, get_attr(BT_CHAR_BLE_TP_IND_ATTR_VAL_INDEX), data, 9);
        BT_WARN("ble tp send indatcate: %d", err);
    }
}

/*************************************************************************
NAME
    ble_tp_notify
*/
static void ble_tp_notify_task(void *pvParameters)
{
    int err = -1;
    char data[244] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};

    while(1)
    {
        err = bt_gatt_notify(ble_tp_conn, get_attr(BT_CHAR_BLE_TP_NOT_ATTR_VAL_INDEX), data, (tx_mtu_size - 3));
        BT_WARN("ble tp send notify : %d", err);
        vTaskDelay(1000);
    }
}

/*************************************************************************
NAME    
    ble_tp_not_ccc_changed
*/ 
static void ble_tp_not_ccc_changed(const struct bt_gatt_attr *attr, u16_t value)
{
    int err;
    BT_WARN("ccc:value=[%d]",value);
    
    if(tp_start)
    {
        if(value == BT_GATT_CCC_NOTIFY)
        {
            if(xTaskCreate(ble_tp_notify_task, (char*)"bletp", 256, NULL, TP_PRIO, &ble_tp_task_h) == pdPASS)
            {
                created_tp_task = 1;
                BT_WARN("Create throughput tx task success.");
            }       
            else        
            {      
                created_tp_task = 0;
                BT_WARN("Create throughput tx task fail.");
            }
        }
        else
        {
            if(created_tp_task)
            {
                BT_WARN("Delete throughput tx task.");
                vTaskDelete(ble_tp_task_h);
                created_tp_task = 0;
            }
        }
    }
    else
    {
        if(created_tp_task)
        {
            BT_WARN("Delete throughput tx task.");
            vTaskDelete(ble_tp_task_h);
            created_tp_task = 0;
        }
        if(value == BT_GATT_CCC_NOTIFY) {
            err = bt_gatt_notify(ble_tp_conn, get_attr(BT_CHAR_BLE_TP_NOT_ATTR_VAL_INDEX), "notify", strlen("notify"));
            BT_WARN("ble tp send indatcate: %d", err);
        }
    }
}

/*************************************************************************
*  DEFINE : attrs 
*/
static struct bt_gatt_attr attrs[]= {
	BT_GATT_PRIMARY_SERVICE(BT_UUID_SVC_BLE_TP),

        BT_GATT_CHARACTERISTIC(BT_UUID_CHAR_BLE_TP_RD,
							BT_GATT_CHRC_READ,
							BT_GATT_PERM_READ,
							ble_tp_recv_rd,
							NULL,
							NULL),

	BT_GATT_CHARACTERISTIC(BT_UUID_CHAR_BLE_TP_WR,
							BT_GATT_CHRC_WRITE |BT_GATT_CHRC_WRITE_WITHOUT_RESP,
							BT_GATT_PERM_WRITE|BT_GATT_PERM_PREPARE_WRITE,
							NULL,
							ble_tp_recv_wr,
							NULL),

	BT_GATT_CHARACTERISTIC(BT_UUID_CHAR_BLE_TP_IND,
							BT_GATT_CHRC_INDICATE,
							0,
							NULL,
							NULL,
							NULL),

	BT_GATT_CCC(ble_tp_ind_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(BT_UUID_CHAR_BLE_TP_NOT,
							BT_GATT_CHRC_NOTIFY,
							0,
							NULL,
							NULL,
							NULL),
							
	BT_GATT_CCC(ble_tp_not_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)

};

static int bl_tp_send_indicate(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				                    const void *data, u16_t len)
{
    //indicate paramete must be allocated statically
    struct bt_gatt_indicate_params *ind_params = k_malloc(sizeof(struct bt_gatt_indicate_params));
    ind_params->attr = attr;
    ind_params->data = data;
    ind_params->len = len;
    ind_params->func = indicate_rsp;
    ind_params->uuid = NULL;
    /*it is possible to indicate by UUID by setting it on the
      parameters, when using this method the attribute given is used as the
      start range when looking up for possible matches.In this case,set uuid as follows.*/
    //ind_params->uuid = attrs[6].uuid;

    return bt_gatt_indicate(conn, ind_params);
}

/*************************************************************************
NAME    
    get_attr
*/
struct bt_gatt_attr *get_attr(u8_t index)
{
	return &attrs[index];
}


struct bt_gatt_service ble_tp_server = BT_GATT_SERVICE(attrs);


/*************************************************************************
NAME    
    ble_tp_init
*/
void ble_tp_init()
{
    if( !isRegister )
    {
        isRegister = 1;
        bt_conn_cb_register(&ble_tp_conn_callbacks);
        bt_gatt_service_register(&ble_tp_server);
    }
}

