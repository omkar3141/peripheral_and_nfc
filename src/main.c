#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>

#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>

#include <dk_buttons_and_leds.h>

#define NFC_FIELD_LED 		DK_LED1
#define BOND_REMOVE_LED     	DK_LED2

#define BUTTON_BOND_REMOVE  	DK_BTN1_MSK

#define NAME_MAX_LEN		(20)
#define LAST_NAME_MAX_LEN 	(20)
#define EMAIL_MAX_LEN 		(30)
#define ADDRESS_MAX_LEN 	(60)
#define PHONE_NUMBER_MAX_LEN 	(15)

#define TOTAL_LEN		(NAME_MAX_LEN + ADDRESS_MAX_LEN + \
				 PHONE_NUMBER_MAX_LEN + LAST_NAME_MAX_LEN + \
				 EMAIL_MAX_LEN)
#define MAX_REC_CNT		(5)

#define APP_ADV_TIMEOUT		(20)


/* Define data container variables */
static uint8_t first_name_value[NAME_MAX_LEN + 1] = {'N', 'a', 'm', 'e'};
static uint8_t address_value[ADDRESS_MAX_LEN + 1] = {'A', 'd', 'd', 'r', 'e', 's', 's'};
static uint8_t phone_number_value[PHONE_NUMBER_MAX_LEN + 1] = {'0', '0', '0', '0', '0', '0', '0'};
static uint8_t last_name_value[LAST_NAME_MAX_LEN + 1] = {'L', 'a', 's', 't', ' ', 'N', 'a', 'm', 'e'};
static uint8_t email_value[EMAIL_MAX_LEN + 1] = {'e', 'm', 'a', 'i', 'l', '@', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm'};
static const uint8_t en_code[] = {'e', 'n'};

/* Buffer used to hold an NFC NDEF message. */
static uint8_t ndef_msg_buf[TOTAL_LEN];

/* Define the custom service UUID */
#define BT_UUID_CUSTOM_SERVICE \
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

static const struct bt_uuid_128 nfc_bizcard_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE);

/* Define the characteristic UUIDs */
/* First Name is a standardized characteristic with UUID 0x2A8A */
static const struct bt_uuid_16 first_name_uuid = BT_UUID_INIT_16(0x2A8A);

/* Last Name is a standardized characteristic with UUID 0x2A90 */
static const struct bt_uuid_16 last_name_uuid = BT_UUID_INIT_16(0x2A90);

/* Email Address is a standardized characteristic with UUID 0x2A87 */
static const struct bt_uuid_16 email_uuid = BT_UUID_INIT_16(0x2A87);

/* Phone Number - using custom UUID since the standardized one doesn't exist */
static const struct bt_uuid_128 phone_number_uuid = BT_UUID_INIT_128(
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3));

/* Address - using custom UUID */
static const struct bt_uuid_128 address_uuid = BT_UUID_INIT_128(
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2));

static struct bt_gatt_cep nfc_bizcard_cep = {
        .properties = BT_GATT_CEP_RELIABLE_WRITE,
};

/* Forward declarations */
static void advertising_start(void);

/***** NFC Handling *****/
/* Callback to process NFC field detection */
static void nfc_callback(void *context, nfc_t2t_event_t event, const uint8_t *data,
			 size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	switch (event) {
		case NFC_T2T_EVENT_FIELD_ON:
			dk_set_led_on(NFC_FIELD_LED);
			break;
		case NFC_T2T_EVENT_FIELD_OFF:
			dk_set_led_off(NFC_FIELD_LED);
			break;
		default:
			break;
	}
}

/* Encode message */
static int msg_encode(uint8_t *buffer, uint32_t *len)
{
	int err;

	/* Create NFC NDEF text record for each business card field in English */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_fn_text_rec, UTF_8, en_code, sizeof(en_code),
				      first_name_value, strlen(first_name_value));

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_ln_text_rec, UTF_8, en_code, sizeof(en_code),
				      last_name_value, strlen(last_name_value));

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_email_text_rec, UTF_8, en_code, sizeof(en_code),
				      email_value, strlen(email_value));

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_addr_text_rec, UTF_8, en_code, sizeof(en_code),
				      address_value, strlen(address_value));

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_pn_text_rec, UTF_8, en_code, sizeof(en_code),
				      phone_number_value, strlen(phone_number_value));

	/* Create NFC NDEF message description, capacity - MAX_REC_COUNT records */
	NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_CNT);

	/* Add text records to NDEF text message */
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					 &NFC_NDEF_TEXT_RECORD_DESC(nfc_fn_text_rec));
	if (err < 0) {
		printk("Cannot add first name record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					 &NFC_NDEF_TEXT_RECORD_DESC(nfc_ln_text_rec));
	if (err < 0) {
		printk("Cannot add last name record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					 &NFC_NDEF_TEXT_RECORD_DESC(nfc_email_text_rec));
	if (err < 0) {
		printk("Cannot add email record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					 &NFC_NDEF_TEXT_RECORD_DESC(nfc_addr_text_rec));
	if (err < 0) {
		printk("Cannot add address record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					 &NFC_NDEF_TEXT_RECORD_DESC(nfc_pn_text_rec));
	if (err < 0) {
		printk("Cannot add phone number record!\n");
		return err;
	}

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), buffer, len);
	if (err < 0) {
		printk("Cannot encode message!\n");
		return err;
	}

	return 0;
}

static void nfc_record_refresh(uint8_t *buffer, uint32_t len)
{
	int err;

	err = msg_encode(buffer, &len);
	if (err < 0) {
		printk("Cannot encode message! err %d\n", err);
		return;
	}

	/* Set created message as the NFC payload */
	err = nfc_t2t_emulation_stop();
	if (err == -NRF_EOPNOTSUPP) {
		printk("NFC already stopped\n");
	}

	err = nfc_t2t_payload_set(buffer, len);
	if (err < 0) {
		printk("Cannot set payload! err %d\n", err);
		return;
	}

	/* Start sensing NFC field */
	err = nfc_t2t_emulation_start();
	if (err < 0) {
		printk("Cannot start emulation! err %d\n", err);
		return;
	}
}

/***** Settings Handling *****/
/* Declare settings callbacks */
static int card_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int card_commit(void);

/* Define static settings handler for user data.
 * Data will be saved  as "card/fn", "card/ln", etc. by using settings_save_one().
 * Data will be retrived in application variables by using single set handler.
 */
SETTINGS_STATIC_HANDLER_DEFINE(bizcard, "card", NULL, card_set, card_commit, NULL);

static int card_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;

	name_len = settings_name_next(name, &next);

	if (!next) {

		if (!strncmp(name, "fn", name_len)) {
			rc = read_cb(cb_arg, first_name_value, sizeof(first_name_value));
			if (rc < 0 || rc > sizeof(first_name_value)) {
				printk("card/fn read failed: rc %d\n", rc);
				return rc;
			}
			printk("card/fn read: %s\n", first_name_value);
			return 0;
		}

		if (!strncmp(name, "ln", name_len)) {
			rc = read_cb(cb_arg, last_name_value, sizeof(last_name_value));
			if (rc < 0 || rc > sizeof(last_name_value)) {
				printk("card/ln read failed: rc %d\n", rc);
				return rc;
			}
			printk("card/ln read: %s\n", last_name_value);
			return 0;
		}

		if (!strncmp(name, "e", name_len)) {
			rc = read_cb(cb_arg, email_value, sizeof(email_value));
			if (rc < 0 || rc > sizeof(email_value)) {
				printk("card/e read failed: rc %d\n", rc);
				return rc;
			}
			printk("card/e read: %s\n", email_value);
			return 0;
		}

		if (!strncmp(name, "a", name_len)) {
			rc = read_cb(cb_arg, address_value, sizeof(address_value));
			if (rc < 0 || rc > sizeof(address_value)) {
				printk("card/a read failed: rc %d\n", rc);
				return rc;
			}
			printk("card/a read: %s\n", address_value);
			return 0;
		}

		if (!strncmp(name, "pn", name_len)) {
			rc = read_cb(cb_arg, phone_number_value, sizeof(phone_number_value));
			if (rc < 0 || rc > sizeof(phone_number_value)) {
				printk("card/pn read failed: rc %d\n", rc);
				return rc;
			}
			printk("card/pn read: %s\n", phone_number_value);
			return 0;
		}
	}

	return -ENOENT;
}

/* Commit callback. Called when all settings have finished loading. */
static int card_commit(void)
{
	printk("Loading NFC tag data\n");

	/* Refresh NFC record */
	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return 0;
}

/***** Bluetooth Custom Service Implementation  *****/
/* Define handling functions for each characteristic */
static ssize_t read_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_long_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > NAME_MAX_LEN) {
		printk("Invalid input length: offset %d, len %d, max %d\n", offset, len, NAME_MAX_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	settings_save_one("card/fn", value, strlen(value) + 1);

	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return len;
}

static ssize_t read_last_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_long_last_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > LAST_NAME_MAX_LEN) {
		printk("Invalid input length: offset %d, len %d, max %d\n", offset, len,
		       LAST_NAME_MAX_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	settings_save_one("card/ln", value, strlen(value) + 1);

	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return len;
}

static ssize_t read_email(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_long_email(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > EMAIL_MAX_LEN) {
		printk("Invalid input length: offset %d, len %d, max %d\n", offset, len, EMAIL_MAX_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	settings_save_one("card/e", value, strlen(value) + 1);

	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return len;
}

static ssize_t read_address(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_long_address(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > ADDRESS_MAX_LEN) {
		printk("Invalid input length: offset %d, len %d, max %d\n", offset, len, ADDRESS_MAX_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	settings_save_one("card/a", value, strlen(value) + 1);

	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return len;
}

static ssize_t read_phone_number(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_long_phone_number(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > PHONE_NUMBER_MAX_LEN) {
		printk("Invalid input length: offset %d, len %d, max %d\n", offset, len, PHONE_NUMBER_MAX_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	settings_save_one("card/pn", value, strlen(value) + 1);

	nfc_record_refresh(ndef_msg_buf, sizeof(ndef_msg_buf));

	return len;
}

/* Define the custom service */
BT_GATT_SERVICE_DEFINE(nfc_bizcard_svc,
        BT_GATT_PRIMARY_SERVICE(&nfc_bizcard_uuid),
        BT_GATT_CHARACTERISTIC(&first_name_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT |
		BT_GATT_PERM_PREPARE_WRITE,
                read_name, write_long_name, first_name_value),
        BT_GATT_CEP(&nfc_bizcard_cep),
        BT_GATT_CHARACTERISTIC(&last_name_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT |
		BT_GATT_PERM_PREPARE_WRITE,
                read_last_name, write_long_last_name, last_name_value),
        BT_GATT_CEP(&nfc_bizcard_cep),
        BT_GATT_CHARACTERISTIC(&email_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT |
		BT_GATT_PERM_PREPARE_WRITE,
                read_email, write_long_email, email_value),
        BT_GATT_CEP(&nfc_bizcard_cep),
        BT_GATT_CHARACTERISTIC(&address_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT |
		BT_GATT_PERM_PREPARE_WRITE,
                read_address, write_long_address, address_value),
        BT_GATT_CEP(&nfc_bizcard_cep),
        BT_GATT_CHARACTERISTIC(&phone_number_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT |
		BT_GATT_PERM_PREPARE_WRITE,
                read_phone_number, write_long_phone_number, phone_number_value),
        BT_GATT_CEP(&nfc_bizcard_cep)
);

/* Define connectable and scannable advertisement */
static const struct bt_data adv_data[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE),
};

static const struct bt_data scan_data[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1)
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

}

/* Authentication callbacks */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed: %s, reason: %d\n", addr, reason);
}

/* Register connection callbacks */
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* Register authentication callbacks */
static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
};

/* Register pairing callbacks */
static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

static void bt_ready(int err)
{
	printk("\nBluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* Register authentication callbacks */
	err = bt_conn_auth_cb_register(&auth_cb_display);
	if (err) {
		printk("Failed to register authentication callbacks (err %d)\n", err);
		return;
	}

	/* Register pairing callbacks */
	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		printk("Failed to register pairing callbacks (err %d)\n", err);
		return;
	}

	advertising_start();
}

static void advertising_start(void)
{
	int err;
	struct bt_le_ext_adv *adv;
	struct bt_le_ext_adv_start_param start_param =
		BT_LE_EXT_ADV_START_PARAM_INIT(APP_ADV_TIMEOUT * 100, 0);

	struct bt_le_adv_param adv_param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_SCANNABLE,
					BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

	/* Create a connectable advertising set */
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
	if (err) {
		printk("Failed to create advertising set (err %d)\n", err);
		return;
	}

	/* Set advertising data to have complete local name set */
	err = bt_le_ext_adv_set_data(adv, adv_data, ARRAY_SIZE(adv_data),
				    scan_data, ARRAY_SIZE(scan_data));
	if (err) {
		printk("Failed to set advertising data (err %d)\n", err);
		return;
	}

	err = bt_le_ext_adv_start(adv, &start_param);
	if (err) {
		printk("Failed to start extended advertising (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started (will auto-stop after %d seconds)\n", APP_ADV_TIMEOUT);
	printk("Passkey authentication enabled - passkey will be displayed when needed\n");
	printk("Press Button 1 to remove all bonds\n");
}

/***** Button Handling *****/
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if ((has_changed & BUTTON_BOND_REMOVE) && (button_state & BUTTON_BOND_REMOVE)) {
		printk("Removing all bonds...\n");

		/* Turn on LED to indicate bond removal in progress */
		dk_set_led_on(BOND_REMOVE_LED);

		/* Delete all bonds */
		if (IS_ENABLED(CONFIG_SETTINGS)) {
			bt_unpair(BT_ID_DEFAULT, NULL);
		}

		printk("All bonds removed\n");

		/* Turn off LED after bond removal */
		k_sleep(K_MSEC(500));
		dk_set_led_off(BOND_REMOVE_LED);
	}
}

int main(void)
{
	int err;

	/* Configure LED-pins as outputs */
	err = dk_leds_init();
	if (err < 0) {
		printk("Cannot init LEDs!\n");
		return err;
	}

	/* Configure buttons */
	err = dk_buttons_init(button_changed);
	if (err < 0) {
		printk("Cannot init buttons (err: %d)\n", err);
		return err;
	}

	/* Set up NFC */
	err = nfc_t2t_setup(nfc_callback, NULL);
	if (err < 0) {
		printk("Cannot setup NFC T2T library!\n");
		return err;
	}

	/* Enable Bluetooth */
        err = bt_enable(bt_ready);
        if (err) {
                printk("Bluetooth initialization failed (err %d)\n", err);
                return err;
        }

	return 0;
}
