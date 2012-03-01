/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef _MODEMMANAGER_ENUMS_H_
#define _MODEMMANAGER_ENUMS_H_

/**
 * SECTION:mm-enums
 * @short_description: Common enumerations and types in the API.
 *
 * This section defines enumerations and types that are used in the
 * ModemManager interface.
 **/

/**
 * MMModemCapability:
 * @MM_MODEM_CAPABILITY_NONE: Modem has no capabilities.
 * @MM_MODEM_CAPABILITY_POTS: Modem supports the analog wired telephone network (ie 56k dialup) and does not have wireless/cellular capabilities.
 * @MM_MODEM_CAPABILITY_CDMA_EVDO: Modem supports at least one of CDMA 1xRTT, EVDO revision 0, EVDO revision A, or EVDO revision B.
 * @MM_MODEM_CAPABILITY_GSM_UMTS: Modem supports at least one of GSM, GPRS, EDGE, UMTS, HSDPA, HSUPA, or HSPA+ packet switched data capability.
 * @MM_MODEM_CAPABILITY_LTE: Modem has LTE data capability.
 * @MM_MODEM_CAPABILITY_LTE_ADVANCED: Modem has LTE Advanced data capability.
 *
 * Flags describing one or more of the general access technology families that a
 * modem supports.
 */
typedef enum { /*< underscore_name=mm_modem_capability >*/
    MM_MODEM_CAPABILITY_NONE         = 0,
    MM_MODEM_CAPABILITY_POTS         = 1 << 1,
    MM_MODEM_CAPABILITY_CDMA_EVDO    = 1 << 2,
    MM_MODEM_CAPABILITY_GSM_UMTS     = 1 << 3,
    MM_MODEM_CAPABILITY_LTE          = 1 << 4,
    MM_MODEM_CAPABILITY_LTE_ADVANCED = 1 << 5,
} MMModemCapability;

/**
 * MMModemLock:
 * @MM_MODEM_LOCK_UNKNOWN: Lock reason unknown.
 * @MM_MODEM_LOCK_NONE: Modem is unlocked.
 * @MM_MODEM_LOCK_SIM_PIN: SIM requires the PIN code.
 * @MM_MODEM_LOCK_SIM_PIN2: SIM requires the PIN2 code.
 * @MM_MODEM_LOCK_SIM_PUK: SIM requires the PUK code.
 * @MM_MODEM_LOCK_SIM_PUK2: SIM requires the PUK2 code.
 * @MM_MODEM_LOCK_PH_SP_PIN: Modem requires the service provider PIN code.
 * @MM_MODEM_LOCK_PH_SP_PUK: Modem requires the service provider PUK code.
 * @MM_MODEM_LOCK_PH_NET_PIN: Modem requires the network PIN code.
 * @MM_MODEM_LOCK_PH_NET_PUK: Modem requires the network PUK code.
 * @MM_MODEM_LOCK_PH_SIM_PIN: Modem requires the PIN code.
 * @MM_MODEM_LOCK_PH_CORP_PIN: Modem requires the corporate PIN code.
 * @MM_MODEM_LOCK_PH_CORP_PUK: Modem requires the corporate PUK code.
 * @MM_MODEM_LOCK_PH_FSIM_PIN: Modem requires the PH-FSIM PIN code.
 * @MM_MODEM_LOCK_PH_FSIM_PUK: Modem requires the PH-FSIM PUK code.
 * @MM_MODEM_LOCK_PH_NETSUB_PIN: Modem requires the network subset PIN code.
 * @MM_MODEM_LOCK_PH_NETSUB_PUK: Modem requires the network subset PUK code.
 *
 * Enumeration of possible lock reasons.
 */
typedef enum { /*< underscore_name=mm_modem_lock >*/
    MM_MODEM_LOCK_UNKNOWN        = 0,
    MM_MODEM_LOCK_NONE           = 1,
    MM_MODEM_LOCK_SIM_PIN        = 2,
    MM_MODEM_LOCK_SIM_PIN2       = 3,
    MM_MODEM_LOCK_SIM_PUK        = 4,
    MM_MODEM_LOCK_SIM_PUK2       = 5,
    MM_MODEM_LOCK_PH_SP_PIN      = 6,
    MM_MODEM_LOCK_PH_SP_PUK      = 7,
    MM_MODEM_LOCK_PH_NET_PIN     = 8,
    MM_MODEM_LOCK_PH_NET_PUK     = 9,
    MM_MODEM_LOCK_PH_SIM_PIN     = 10,
    MM_MODEM_LOCK_PH_CORP_PIN    = 11,
    MM_MODEM_LOCK_PH_CORP_PUK    = 12,
    MM_MODEM_LOCK_PH_FSIM_PIN    = 13,
    MM_MODEM_LOCK_PH_FSIM_PUK    = 14,
    MM_MODEM_LOCK_PH_NETSUB_PIN  = 15,
    MM_MODEM_LOCK_PH_NETSUB_PUK  = 16
} MMModemLock;

/**
 * MMModemState:
 * @MM_MODEM_STATE_UNKNOWN: State unknown or not reportable.
 * @MM_MODEM_STATE_LOCKED: The modem needs to be unlocked.
 * @MM_MODEM_STATE_DISABLED: The modem is not enabled and is powered down.
 * @MM_MODEM_STATE_DISABLING: The modem is currently transitioning to the @MM_MODEM_STATE_DISABLED state.
 * @MM_MODEM_STATE_ENABLING: The modem is currently transitioning to the @MM_MODEM_STATE_ENABLED state.
 * @MM_MODEM_STATE_ENABLED: The modem is enabled and powered on but not registered with a network provider and not available for data connections.
 * @MM_MODEM_STATE_SEARCHING: The modem is searching for a network provider to register with.
 * @MM_MODEM_STATE_REGISTERED: The modem is registered with a network provider, and data connections and messaging may be available for use.
 * @MM_MODEM_STATE_DISCONNECTING: The modem is disconnecting and deactivating the last active packet data bearer. This state will not be entered if more than one packet data bearer is active and one of the active bearers is deactivated.
 * @MM_MODEM_STATE_CONNECTING: The modem is activating and connecting the first packet data bearer. Subsequent bearer activations when another bearer is already active do not cause this state to be entered.
 * @MM_MODEM_STATE_CONNECTED: One or more packet data bearers is active and connected.
 *
 * Enumeration of possible modem states.
 */
typedef enum { /*< underscore_name=mm_modem_state >*/
    MM_MODEM_STATE_UNKNOWN       = 0,
    MM_MODEM_STATE_LOCKED        = 1,
    MM_MODEM_STATE_DISABLED      = 2,
    MM_MODEM_STATE_DISABLING     = 3,
    MM_MODEM_STATE_ENABLING      = 4,
    MM_MODEM_STATE_ENABLED       = 5,
    MM_MODEM_STATE_SEARCHING     = 6,
    MM_MODEM_STATE_REGISTERED    = 7,
    MM_MODEM_STATE_DISCONNECTING = 8,
    MM_MODEM_STATE_CONNECTING    = 9,
    MM_MODEM_STATE_CONNECTED     = 10
} MMModemState;

/**
 * MMModemStateChangeReason:
 * @MM_MODEM_STATE_CHANGE_REASON_UNKNOWN: Reason unknown or not reportable.
 * @MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED: State change was requested by an interface user.
 * @MM_MODEM_STATE_CHANGE_REASON_SUSPEND: State change was caused by a system suspend.
 *
 * Enumeration of possible reasons to have changed the modem state.
 */
typedef enum { /*< underscore_name=mm_modem_state_change_reason >*/
    MM_MODEM_STATE_CHANGE_REASON_UNKNOWN        = 0,
    MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED = 1,
    MM_MODEM_STATE_CHANGE_REASON_SUSPEND        = 2,
} MMModemStateChangeReason;

/**
 * MMModemAccessTech:
 * @MM_MODEM_ACCESS_TECH_UNKNOWN: The access technology used is unknown.
 * @MM_MODEM_ACCESS_TECH_POTS: Analog wireline telephone.
 * @MM_MODEM_ACCESS_TECH_GSM: GSM.
 * @MM_MODEM_ACCESS_TECH_GSM_COMPACT: Compact GSM.
 * @MM_MODEM_ACCESS_TECH_GPRS: GPRS.
 * @MM_MODEM_ACCESS_TECH_EDGE: EDGE (ETSI 27.007: "GSM w/EGPRS").
 * @MM_MODEM_ACCESS_TECH_UMTS: UMTS (ETSI 27.007: "UTRAN").
 * @MM_MODEM_ACCESS_TECH_HSDPA: HSDPA (ETSI 27.007: "UTRAN w/HSDPA").
 * @MM_MODEM_ACCESS_TECH_HSUPA: HSUPA (ETSI 27.007: "UTRAN w/HSUPA").
 * @MM_MODEM_ACCESS_TECH_HSPA: HSPA (ETSI 27.007: "UTRAN w/HSDPA and HSUPA").
 * @MM_MODEM_ACCESS_TECH_HSPA_PLUS: HSPA+ (ETSI 27.007: "UTRAN w/HSPA+").
 * @MM_MODEM_ACCESS_TECH_1XRTT: CDMA2000 1xRTT.
 * @MM_MODEM_ACCESS_TECH_EVDO0: CDMA2000 EVDO revision 0.
 * @MM_MODEM_ACCESS_TECH_EVDOA: CDMA2000 EVDO revision A.
 * @MM_MODEM_ACCESS_TECH_EVDOB: CDMA2000 EVDO revision B.
 * @MM_MODEM_ACCESS_TECH_LTE: LTE (ETSI 27.007: "E-UTRAN")
 *
 * Describes various access technologies that a device uses when registered with
 * or connected to a network.
 */
typedef enum { /*< underscore_name=mm_modem_access_tech >*/
    MM_MODEM_ACCESS_TECH_UNKNOWN     = 0,
    MM_MODEM_ACCESS_TECH_POTS        = 1,
    MM_MODEM_ACCESS_TECH_GSM         = 2,
    MM_MODEM_ACCESS_TECH_GSM_COMPACT = 3,
    MM_MODEM_ACCESS_TECH_GPRS        = 4,
    MM_MODEM_ACCESS_TECH_EDGE        = 5,
    MM_MODEM_ACCESS_TECH_UMTS        = 6,
    MM_MODEM_ACCESS_TECH_HSDPA       = 7,
    MM_MODEM_ACCESS_TECH_HSUPA       = 8,
    MM_MODEM_ACCESS_TECH_HSPA        = 9,
    MM_MODEM_ACCESS_TECH_HSPA_PLUS   = 10,
    MM_MODEM_ACCESS_TECH_1XRTT       = 11,
    MM_MODEM_ACCESS_TECH_EVDO0       = 12,
    MM_MODEM_ACCESS_TECH_EVDOA       = 13,
    MM_MODEM_ACCESS_TECH_EVDOB       = 14,
    MM_MODEM_ACCESS_TECH_LTE         = 15,
} MMModemAccessTech;

/**
 * MMModemMode:
 * @MM_MODEM_MODE_NONE: None.
 * @MM_MODEM_MODE_1G: CSD, GSM.
 * @MM_MODEM_MODE_2G: GPRS, EDGE.
 * @MM_MODEM_MODE_3G: UMTS, HSxPA.
 * @MM_MODEM_MODE_4G: LTE.
 * @MM_MODEM_MODE_ANY: Any mode can be used (only this value allowed for POTS modems).
 *
 * Bitfield to indicate which access modes are supported, allowed or
 * preferred in a given device.
 */
typedef enum { /*< underscore_name=mm_modem_mode >*/
    MM_MODEM_MODE_NONE = 0,
    MM_MODEM_MODE_1G   = 1 << 0,
    MM_MODEM_MODE_2G   = 1 << 1,
    MM_MODEM_MODE_3G   = 1 << 2,
    MM_MODEM_MODE_4G   = 1 << 3,
    MM_MODEM_MODE_ANY  = 0xFFFFFFFF
} MMModemMode;

/**
 * MMModemBand:
 * @MM_MODEM_BAND_UNKNOWN: Unknown or invalid band.
 * @MM_MODEM_BAND_EGSM: GSM/GPRS/EDGE 900 MHz.
 * @MM_MODEM_BAND_DCS: GSM/GPRS/EDGE 1800 MHz.
 * @MM_MODEM_BAND_PCS: GSM/GPRS/EDGE 1900 MHz.
 * @MM_MODEM_BAND_G850: GSM/GPRS/EDGE 850 MHz.
 * @MM_MODEM_BAND_U2100: WCDMA 2100 MHz (Class I).
 * @MM_MODEM_BAND_U1800: WCDMA 3GPP 1800 MHz (Class III).
 * @MM_MODEM_BAND_U17IV: WCDMA 3GPP AWS 1700/2100 MHz (Class IV).
 * @MM_MODEM_BAND_U800: WCDMA 3GPP UMTS 800 MHz (Class VI).
 * @MM_MODEM_BAND_U850: WCDMA 3GPP UMTS 850 MHz (Class V).
 * @MM_MODEM_BAND_U900: WCDMA 3GPP UMTS 900 MHz (Class VIII).
 * @MM_MODEM_BAND_U17IX: WCDMA 3GPP UMTS 1700 MHz (Class IX).
 * @MM_MODEM_BAND_U1900: WCDMA 3GPP UMTS 1900 MHz (Class II).
 * @MM_MODEM_BAND_U2600: WCDMA 3GPP UMTS 2600 MHz (Class VII, internal).
 * @MM_MODEM_BAND_CDMA_BC0_CELLULAR_800: CDMA Band Class 0 (US Cellular 850MHz).
 * @MM_MODEM_BAND_CDMA_BC1_PCS_1900: CDMA Band Class 1 (US PCS 1900MHz).
 * @MM_MODEM_BAND_CDMA_BC2_TACS: CDMA Band Class 2 (UK TACS 900MHz).
 * @MM_MODEM_BAND_CDMA_BC3_JTACS: CDMA Band Class 3 (Japanese TACS).
 * @MM_MODEM_BAND_CDMA_BC4_KOREAN_PCS: CDMA Band Class 4 (Korean PCS).
 * @MM_MODEM_BAND_CDMA_BC5_NMT450: CDMA Band Class 5 (NMT 450MHz).
 * @MM_MODEM_BAND_CDMA_BC6_IMT2000: CDMA Band Class 6 (IMT2000 2100MHz).
 * @MM_MODEM_BAND_CDMA_BC7_CELLULAR_700: CDMA Band Class 7 (Cellular 700MHz).
 * @MM_MODEM_BAND_CDMA_BC8_1800: CDMA Band Class 8 (1800MHz).
 * @MM_MODEM_BAND_CDMA_BC9_900: CDMA Band Class 9 (900MHz).
 * @MM_MODEM_BAND_CDMA_BC10_SECONDARY_800: CDMA Band Class 10 (US Secondary 800).
 * @MM_MODEM_BAND_CDMA_BC11_PAMR_400: CDMA Band Class 11 (European PAMR 400MHz).
 * @MM_MODEM_BAND_CDMA_BC12_PAMR_800: CDMA Band Class 12 (PAMR 800MHz).
 * @MM_MODEM_BAND_CDMA_BC13_IMT2000_2500: CDMA Band Class 13 (IMT2000 2500MHz Expansion).
 * @MM_MODEM_BAND_CDMA_BC14_PCS2_1900: CDMA Band Class 14 (More US PCS 1900MHz).
 * @MM_MODEM_BAND_CDMA_BC15_AWS: CDMA Band Class 15 (AWS 1700MHz).
 * @MM_MODEM_BAND_CDMA_BC16_US_2500: CDMA Band Class 16 (US 2500MHz).
 * @MM_MODEM_BAND_CDMA_BC17_US_FLO_2500: CDMA Band Class 17 (US 2500MHz Forward Link Only).
 * @MM_MODEM_BAND_CDMA_BC18_US_PS_700: CDMA Band Class 18 (US 700MHz Public Safety).
 * @MM_MODEM_BAND_CDMA_BC19_US_LOWER_700: CDMA Band Class 19 (US Lower 700MHz).
 * @MM_MODEM_BAND_ANY: For certain operations, allow the modem to select a band automatically.
 *
 * A 64-bit wide bitfield describing the specific radio bands supported by
 * the device and the radio bands the device is allowed to use when
 * connecting to a mobile network.
 */
typedef enum { /*< skip >*/
    MM_MODEM_BAND_UNKNOWN = 0,
    /* GSM/UMTS/3GPP bands */
    MM_MODEM_BAND_EGSM  = 1 << 0,
    MM_MODEM_BAND_DCS   = 1 << 1,
    MM_MODEM_BAND_PCS   = 1 << 2,
    MM_MODEM_BAND_G850  = 1 << 3,
    MM_MODEM_BAND_U2100 = 1 << 4,
    MM_MODEM_BAND_U1800 = 1 << 5,
    MM_MODEM_BAND_U17IV = 1 << 6,
    MM_MODEM_BAND_U800  = 1 << 7,
    MM_MODEM_BAND_U850  = 1 << 8,
    MM_MODEM_BAND_U900  = 1 << 9,
    MM_MODEM_BAND_U17IX = 1 << 10,
    MM_MODEM_BAND_U1900 = 1 << 11,
    MM_MODEM_BAND_U2600 = 1 << 12,
    /* CDMA Band Classes (see 3GPP2 C.S0057-C) */
    MM_MODEM_BAND_CDMA_BC0_CELLULAR_800   = 1ULL << 32,
    MM_MODEM_BAND_CDMA_BC1_PCS_1900       = 1ULL << 33,
    MM_MODEM_BAND_CDMA_BC2_TACS           = 1ULL << 34,
    MM_MODEM_BAND_CDMA_BC3_JTACS          = 1ULL << 35,
    MM_MODEM_BAND_CDMA_BC4_KOREAN_PCS     = 1ULL << 36,
    MM_MODEM_BAND_CDMA_BC5_NMT450         = 1ULL << 37,
    MM_MODEM_BAND_CDMA_BC6_IMT2000        = 1ULL << 38,
    MM_MODEM_BAND_CDMA_BC7_CELLULAR_700   = 1ULL << 49,
    MM_MODEM_BAND_CDMA_BC8_1800           = 1ULL << 40,
    MM_MODEM_BAND_CDMA_BC9_900            = 1ULL << 41,
    MM_MODEM_BAND_CDMA_BC10_SECONDARY_800 = 1ULL << 42,
    MM_MODEM_BAND_CDMA_BC11_PAMR_400      = 1ULL << 43,
    MM_MODEM_BAND_CDMA_BC12_PAMR_800      = 1ULL << 44,
    MM_MODEM_BAND_CDMA_BC13_IMT2000_2500  = 1ULL << 45,
    MM_MODEM_BAND_CDMA_BC14_PCS2_1900     = 1ULL << 46,
    MM_MODEM_BAND_CDMA_BC15_AWS           = 1ULL << 47,
    MM_MODEM_BAND_CDMA_BC16_US_2500       = 1ULL << 48,
    MM_MODEM_BAND_CDMA_BC17_US_FLO_2500   = 1ULL << 49,
    MM_MODEM_BAND_CDMA_BC18_US_PS_700     = 1ULL << 50,
    MM_MODEM_BAND_CDMA_BC19_US_LOWER_700  = 1ULL << 51,
    /* All/Any */
    MM_MODEM_BAND_ANY = 0xFFFFFFFFFFFFFFFF
} MMModemBand;

/**
 * MMModemSmsState:
 * @MM_MODEM_SMS_STATE_UNKNOWN: State unknown or not reportable.
 * @MM_MODEM_SMS_STATE_STORED: The message has been neither received nor yet sent.
 * @MM_MODEM_SMS_STATE_RECEIVING: The message is being received but is not yet complete.
 * @MM_MODEM_SMS_STATE_RECEIVED: The message has been completely received.
 * @MM_MODEM_SMS_STATE_SENDING: The message is queued for delivery.
 * @MM_MODEM_SMS_STATE_SENT: The message was successfully sent.
 *
 * State of a given SMS.
 */
typedef enum { /*< underscore_name=mm_modem_sms_state >*/
    MM_MODEM_SMS_STATE_UNKNOWN   = 0,
    MM_MODEM_SMS_STATE_STORED    = 1,
    MM_MODEM_SMS_STATE_RECEIVING = 2,
    MM_MODEM_SMS_STATE_RECEIVED  = 3,
    MM_MODEM_SMS_STATE_SENDING   = 4,
    MM_MODEM_SMS_STATE_SENT      = 5,
} MMModemSmsState;

/**
 * MMModemLocationSource:
 * @MM_MODEM_LOCATION_SOURCE_NONE: None.
 * @MM_MODEM_LOCATION_SOURCE_GSM_LAC_CI: Location Area Code and Cell ID.
 * @MM_MODEM_LOCATION_SOURCE_GPS_RAW: GPS location given by predefined keys.
 * @MM_MODEM_LOCATION_SOURCE_GPS_NMEA: GPS location given as NMEA traces.
 *
 * Sources of location information supported by the modem.
 */
typedef enum { /*< underscore_name=mm_modem_location_source >*/
    MM_MODEM_LOCATION_SOURCE_NONE       = 0,
    MM_MODEM_LOCATION_SOURCE_GSM_LAC_CI = 1 << 0,
    MM_MODEM_LOCATION_SOURCE_GPS_RAW    = 1 << 1,
    MM_MODEM_LOCATION_SOURCE_GPS_NMEA   = 1 << 2,
} MMModemLocationSource;

/**
 * MMModemContactsStorage:
 * @MM_MODEM_CONTACTS_STORAGE_UNKNOWN: Unknown location.
 * @MM_MODEM_CONTACTS_STORAGE_ME: Device's local memory.
 * @MM_MODEM_CONTACTS_STORAGE_SM: Card inserted in the device (like a SIM/RUIM).
 * @MM_MODEM_CONTACTS_STORAGE_MT: Combined device/ME and SIM/SM phonebook.
 *
 * Specifies different storage locations for contact information.
 */
typedef enum { /*< underscore_name=mm_modem_contacts_storage >*/
    MM_MODEM_CONTACTS_STORAGE_UNKNOWN = 0,
    MM_MODEM_CONTACTS_STORAGE_ME      = 1,
    MM_MODEM_CONTACTS_STORAGE_SM      = 2,
    MM_MODEM_CONTACTS_STORAGE_MT      = 3,
} MMModemContactsStorage;

/**
 * MMBearerIpMethod:
 * @MM_BEARER_IP_METHOD_UNKNOWN: Unknown method.
 * @MM_BEARER_IP_METHOD_PPP: Use PPP to get the address.
 * @MM_BEARER_IP_METHOD_STATIC: Use the provided static IP configuration given by the modem to configure the IP data interface.
 * @MM_BEARER_IP_METHOD_DHCP: Begin DHCP on the data interface to obtain necessary IP configuration details.
 *
 * Type of IP method configuration to be used in a given Bearer.
 */
typedef enum { /*< underscore_name=mm_bearer_ip_method >*/
    MM_BEARER_IP_METHOD_UNKNOWN = 0,
    MM_BEARER_IP_METHOD_PPP     = 1,
    MM_BEARER_IP_METHOD_STATIC  = 2,
    MM_BEARER_IP_METHOD_DHCP    = 3,
} MMBearerIpMethod;

/**
 * MMModemCdmaRegistrationState:
 * @MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN: Registration status is unknown or the device is not registered.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED: Registered, but roaming status is unknown or cannot be provided by the device. The device may or may not be roaming.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_HOME: Currently registered on the home network.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING: Currently registered on a roaming network.
 *
 * Registration state of a CDMA modem.
 */
typedef enum { /*< underscore_name=mm_modem_cdma_registration_state >*/
    MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN    = 0,
    MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED = 1,
    MM_MODEM_CDMA_REGISTRATION_STATE_HOME       = 2,
    MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING    = 3,
} MMModemCdmaRegistrationState;

/**
 * MMModemCdmaActivationState:
 * @MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED: Device is not activated
 * @MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING: Device is activating
 * @MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED: Device is partially activated; carrier-specific steps required to continue.
 * @MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED: Device is ready for use.
 *
 * Activation state of a CDMA modem.
 */
typedef enum { /*< underscore_name=mm_modem_cdma_activation_state >*/
    MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED       = 0,
    MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING          = 1,
    MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED = 2,
    MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED           = 3,
} MMModemCdmaActivationState;

/**
 * MMModemCdmaActivationError:
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_NONE: No error.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_UNKNOWN: An error occurred.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_ROAMING: Device cannot activate while roaming.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE: Device cannot activate on this network type (eg EVDO vs 1xRTT).
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT: Device could not connect to the network for activation.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED: Device could not authenticate to the network for activation.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED: Later stages of device provisioning failed.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_NO_SIGNAL: No signal available.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_TIMED_OUT: Activation timed out.
 * @MM_MODEM_CDMA_ACTIVATION_ERROR_START_FAILED: API call for initial activation failed.
 */
typedef enum { /*< underscore_name=mm_modem_cdma_activation_error >*/
    MM_MODEM_CDMA_ACTIVATION_ERROR_NONE                           = 0,
    MM_MODEM_CDMA_ACTIVATION_ERROR_UNKNOWN                        = 1,
    MM_MODEM_CDMA_ACTIVATION_ERROR_ROAMING                        = 2,
    MM_MODEM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE          = 3,
    MM_MODEM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT              = 4,
    MM_MODEM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED = 5,
    MM_MODEM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED            = 6,
    MM_MODEM_CDMA_ACTIVATION_ERROR_NO_SIGNAL                      = 7,
    MM_MODEM_CDMA_ACTIVATION_ERROR_TIMED_OUT                      = 8,
    MM_MODEM_CDMA_ACTIVATION_ERROR_START_FAILED                   = 9,
} MMModemCdmaActivationError;

/**
 * MMModem3gppRegistrationState:
 * @MM_MODEM_3GPP_REGISTRATION_STATE_IDLE: Not registered, not searching for new operator to register.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_HOME: Registered on home network.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING: Not registered, searching for new operator to register with.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_DENIED: Registration denied.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN: Unknown registration status.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING: Registered on a roaming network.
 *
 * GSM registration code as defined in 3GPP TS 27.007 section 10.1.19.
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_registration_state >*/
    MM_MODEM_3GPP_REGISTRATION_STATE_IDLE      = 0,
    MM_MODEM_3GPP_REGISTRATION_STATE_HOME      = 1,
    MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING = 2,
    MM_MODEM_3GPP_REGISTRATION_STATE_DENIED    = 3,
    MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN   = 4,
    MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING   = 5,
} MMModem3gppRegistrationState;

/**
 * MMModem3gppFacility:
 * @MM_MODEM_3GPP_FACILITY_NONE: No facility.
 * @MM_MODEM_3GPP_FACILITY_SIM: SIM lock.
 * @MM_MODEM_3GPP_FACILITY_FIXED_DIALING: Fixed dialing (PIN2) SIM lock.
 * @MM_MODEM_3GPP_FACILITY_PH_SIM: Device is locked to a specific SIM.
 * @MM_MODEM_3GPP_FACILITY_PH_FSIM: Device is locked to first SIM inserted.
 * @MM_MODEM_3GPP_FACILITY_NET_PERS: Network personalization.
 * @MM_MODEM_3GPP_FACILITY_NET_SUB_PERS: Network subset personalization.
 * @MM_MODEM_3GPP_FACILITY_PROVIDER_PERS: Service provider personalization.
 * @MM_MODEM_3GPP_FACILITY_CORP_PERS: Corporate personalization.
 *
 * A bitfield describing which facilities have a lock enabled, i.e.,
 * requires a pin or unlock code. The facilities include the
 * personalizations (device locks) described in 3GPP spec TS 22.022,
 * and the PIN and PIN2 locks, which are SIM locks.
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_facility >*/
    MM_MODEM_3GPP_FACILITY_NONE          = 0x00,
    MM_MODEM_3GPP_FACILITY_SIM           = 0x01,
    MM_MODEM_3GPP_FACILITY_FIXED_DIALING = 0x02,
    MM_MODEM_3GPP_FACILITY_PH_SIM        = 0x04,
    MM_MODEM_3GPP_FACILITY_PH_FSIM       = 0x08,
    MM_MODEM_3GPP_FACILITY_NET_PERS      = 0x10,
    MM_MODEM_3GPP_FACILITY_NET_SUB_PERS  = 0x20,
    MM_MODEM_3GPP_FACILITY_PROVIDER_PERS = 0x40,
    MM_MODEM_3GPP_FACILITY_CORP_PERS     = 0x80
} MMModem3gppFacility;

/**
 * MMModem3gppNetworkAvailability:
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN: Unknown availability.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE: Network is available.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT: Network is the current one.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN: Network is forbidden.
 *
 * Network availability status as defined in 3GPP TS 27.007 section 7.3
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_network_availability >*/
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN   = 0,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE = 1,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT   = 2,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN = 3,
} MMModem3gppNetworkAvailability;

/**
 * MMModem3gppUssdSessionState:
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE: No active session.
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE: A session is active and the mobile is waiting for a response.
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE: The network is waiting for the client's response.
 *
 * State of a USSD session.
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_ussd_session_state >*/
    MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE          = 0,
    MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE        = 1,
    MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE = 2,
} MMModem3gppUssdSessionState;

#endif /*  _MODEMMANAGER_ENUMS_H_ */
