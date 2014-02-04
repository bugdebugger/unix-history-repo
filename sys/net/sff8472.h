/*-
 * Copyright (c) 2013 George V. Neville-Neil
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * The following set of constants are from Document SFF-8472
 * "Diagnostic Monitoring Interface for Optical Transceivers" revision
 * 11.3 published by the SFF Committee on June 11, 2013
 *
 * The SFF standard defines two ranges of addresses, each 255 bytes
 * long for the storage of data and diagnostics on cables, such as
 * SFP+ optics and TwinAx cables.  The ranges are defined in the
 * following way:
 *
 * Base Address 0xa0 (Identification Data)
 * 0-95    Serial ID Defined by SFP MSA
 * 96-127  Vendor Specific Data
 * 128-255 Reserved
 *
 * Base Address 0xa2 (Diagnostic Data)
 * 0-55    Alarm and Warning Thresholds
 * 56-95   Cal Constants
 * 96-119  Real Time Diagnostic Interface
 * 120-127 Vendor Specific
 * 128-247 User Writable EEPROM
 * 248-255 Vendor Specific
 *
 * Note that not all addresses are supported.  Where support is
 * optional this is noted and instructions for checking for the
 * support are supplied.
 *
 * All these values are read across an I2C (i squared C) bus.  Any
 * device wishing to read these addresses must first have support for
 * i2c calls.  The Chelsio T4/T5 driver (dev/cxgbe) is one such
 * driver.
 */


/* Table 3.1 Two-wire interface ID: Data Fields */

#define SFF_8472_BASE 		0xa0   /* Base address for all our queries. */
#define SFF_8472_ID		0  /* Transceiver Type (Table 3.2) */
#define SFF_8472_EXT_ID		1  /* Extended transceiver type (Table 3.3) */
#define SFF_8472_CONNECTOR	2  /* Connector type (Table 3.4) */
#define SFF_8472_TRANS_START	3  /* Elec or Optical Compatibility
				    * (Table 3.5) */
#define SFF_8472_TRANS_END	10
#define SFF_8472_ENCODING	11 /* Encoding Code for high speed
				    * serial encoding algorithm (see
				    * Table 3.6) */
#define SFF_8472_BITRATE	12 /* Nominal signaling rate, units
				    *  of 100MBd.  (see details for
				    *  rates > 25.0Gb/s) */
#define SFF_8472_RATEID		13 /* Type of rate select
				    * functionality (see Table
				    * 3.6a) */
#define SFF_8472_LEN_SMF_KM	14 /* Link length supported for single
				    * mode fiber, units of km */
#define SFF_8472_LEN_SMF	15 /* Link length supported for single
				    * mode fiber, units of 100 m */
#define SFF_8472_LEN_50UM	16 /* Link length supported for 50 um
				    * OM2 fiber, units of 10 m */
#define SFF_8472_LEN_625UM	17 /* Link length supported for 62.5
				    * um OM1 fiber, units of 10 m */
#define SFF_8472_LEN_OM4	18 /* Link length supported for 50um
				    * OM4 fiber, units of 10m.
				    * Alternatively copper or direct
				    * attach cable, units of m */
#define SFF_8472_LEN_OM3	19 /* Link length supported for 50 um OM3 fiber, units of 10 m */
#define SFF_8472_VENDOR_START 	20 /* Vendor name [Address A0h, Bytes
				    * 20-35] */
#define SFF_8472_VENDOR_END 	35
#define SFF_8472_TRANS		36 /* Transceiver Code for electronic
				    * or optical compatibility (see
				    * Table 3.5) */
#define SFF_8472_VENDOR_OUI_START	37 /* Vendor OUI SFP vendor IEEE
				    * company ID */
#define SFF_8472_VENDOR_OUI_END	39
#define SFF_8472_PN_START 	40 /* Vendor PN */
#define SFF_8472_PN_END 	55
#define SFF_8472_REV_START 	56 /* Vendor Revision */
#define SFF_8472_REV_END 	59
#define SFF_8472_WAVELEN_START	60 /* Wavelength Laser wavelength
				    * (Passive/Active Cable
				    * Specification Compliance) */
#define SFF_8472_WAVELEN_END	61
#define SFF_8472_CC_BASE	63 /* CC_BASE Check code for Base ID
				    * Fields (addresses 0 to 62) */

/*
 * Extension Fields (optional) check the options before reading other
 * addresses.
 */
#define SFF_8472_OPTIONS_MSB	64 /* Options Indicates which optional
				    * transceiver signals are
				    * implemented */
#define SFF_8472_OPTIONS_LSB	65 /* (see Table 3.7) */
#define SFF_8472_BR_MAX		66 /* BR max Upper bit rate margin,
				    * units of % (see details for
				    * rates > 25.0Gb/s) */
#define SFF_8472_BR_MIN		67 /* Lower bit rate margin, units of
				    * % (see details for rates >
				    * 25.0Gb/s) */
#define SFF_8472_SN_START 	68 /* Vendor SN [Address A0h, Bytes 68-83] */
#define SFF_8472_SN_END 	83
#define SFF_8472_DATE_START	84 /* Date code Vendor’s manufacturing
				    * date code (see Table 3.8) */
#define SFF_8472_DATE_END	91
#define SFF_8472_DIAG_TYPE	92 /* Diagnostic Monitoring Type
				    * Indicates which type of
				    * diagnostic monitoring is
				    * implemented (if any) in the
				    * transceiver (see Table 3.9)
				    */
#define SFF_8472_DIAG_IMPL	(1 << 6) /* Required to be 1 */
#define SFF_8472_DIAG_INTERNAL	(1 << 5) /* Internal measurements. */
#define SFF_8472_DIAG_EXTERNAL	(1 << 4) /* External measurements. */
#define SFF_8472_DIAG_POWER	(1 << 3) /* Power measurement type */
#define SFF_8472_DIAG_ADDR_CHG	(1 << 2) /* Address change required.
					  * See SFF-8472 doc. */

#define SFF_8472_ENHANCED	93 /* Enhanced Options Indicates which
				    * optional enhanced features are
				    * implemented (if any) in the
				    * transceiver (see Table 3.10) */
#define SFF_8472_COMPLIANCE	94 /* SFF-8472 Compliance Indicates
				    * which revision of SFF-8472 the
				    * transceiver complies with.  (see
				    * Table 3.12)*/
#define SFF_8472_CC_EXT		95 /* Check code for the Extended ID
				    * Fields (addresses 64 to 94)
				    */

#define SFF_8472_VENDOR_RSRVD_START	96
#define SFF_8472_VENDOR_RSRVD_END	127

#define SFF_8472_RESERVED_START	128
#define SFF_8472_RESERVED_END	255

 /*
  * Diagnostics are available at the two wire address 0xa2.  All
  * diagnostics are OPTIONAL so you should check 0xa0 registers 92 to
  * see which, if any are supported.
  */

#define SFF_8472_DIAG 		0xa2  /* Base address for diagnostics. */

 /*
  *  Table 3.15 Alarm and Warning Thresholds All values are 2 bytes
  * and MUST be read in a single read operation starting at the MSB
  */

#define SFF_8472_TEMP_HIGH_ALM		0 /* Temp High Alarm  */
#define SFF_8472_TEMP_LOW_ALM		2 /* Temp Low Alarm */
#define SFF_8472_TEMP_HIGH_WARN		4 /* Temp High Warning */
#define SFF_8472_TEMP_LOW_WARN		6 /* Temp Low Warning */
#define SFF_8472_VOLTAGE_HIGH_ALM	8 /* Voltage High Alarm */
#define SFF_8472_VOLTAGE_LOW_ALM	10 /* Voltage Low Alarm */
#define SFF_8472_VOLTAGE_HIGH_WARN	12 /* Voltage High Warning */
#define SFF_8472_VOLTAGE_LOW_WARN	14 /* Voltage Low Warning */
#define SFF_8472_BIAS_HIGH_ALM		16 /* Bias High Alarm */
#define SFF_8472_BIAS_LOW_ALM		18 /* Bias Low Alarm */
#define SFF_8472_BIAS_HIGH_WARN		20 /* Bias High Warning */
#define SFF_8472_BIAS_LOW_WARN		22 /* Bias Low Warning */
#define SFF_8472_TX_POWER_HIGH_ALM	24 /* TX Power High Alarm */
#define SFF_8472_TX_POWER_LOW_ALM	26 /* TX Power Low Alarm */
#define SFF_8472_TX_POWER_HIGH_WARN	28 /* TX Power High Warning */
#define SFF_8472_TX_POWER_LOW_WARN	30 /* TX Power Low Warning */
#define SFF_8472_RX_POWER_HIGH_ALM	32 /* RX Power High Alarm */
#define SFF_8472_RX_POWER_LOW_ALM	34 /* RX Power Low Alarm */
#define SFF_8472_RX_POWER_HIGH_WARN	36 /* RX Power High Warning */
#define SFF_8472_RX_POWER_LOW_WARN	38 /* RX Power Low Warning */

#define SFF_8472_RX_POWER4	56 /* Rx_PWR(4) Single precision
				    *  floating point calibration data
				    *  - Rx optical power. Bit 7 of
				    *  byte 56 is MSB. Bit 0 of byte
				    *  59 is LSB. Rx_PWR(4) should be
				    *  set to zero for “internally
				    *  calibrated” devices. */
#define SFF_8472_RX_POWER3	60 /* Rx_PWR(3) Single precision
				    * floating point calibration data
				    * - Rx optical power.  Bit 7 of
				    * byte 60 is MSB. Bit 0 of byte 63
				    * is LSB. Rx_PWR(3) should be set
				    * to zero for “internally
				    * calibrated” devices.*/
#define SFF_8472_RX_POWER2	64 /* Rx_PWR(2) Single precision
				    * floating point calibration data,
				    * Rx optical power.  Bit 7 of byte
				    * 64 is MSB, bit 0 of byte 67 is
				    * LSB. Rx_PWR(2) should be set to
				    * zero for “internally calibrated”
				    * devices. */
#define SFF_8472_RX_POWER1	68 /* Rx_PWR(1) Single precision
				    * floating point calibration data,
				    * Rx optical power. Bit 7 of byte
				    * 68 is MSB, bit 0 of byte 71 is
				    * LSB. Rx_PWR(1) should be set to
				    * 1 for “internally calibrated”
				    * devices. */
#define SFF_8472_RX_POWER0	72 /* Rx_PWR(0) Single precision
				    * floating point calibration data,
				    * Rx optical power. Bit 7 of byte
				    * 72 is MSB, bit 0 of byte 75 is
				    * LSB. Rx_PWR(0) should be set to
				    * zero for “internally calibrated”
				    * devices. */
#define SFF_8472_TX_I_SLOPE	76 /* Tx_I(Slope) Fixed decimal
				    * (unsigned) calibration data,
				    * laser bias current. Bit 7 of
				    * byte 76 is MSB, bit 0 of byte 77
				    * is LSB. Tx_I(Slope) should be
				    * set to 1 for “internally
				    * calibrated” devices. */
#define SFF_8472_TX_I_OFFSET	78 /* Tx_I(Offset) Fixed decimal
				    * (signed two’s complement)
				    * calibration data, laser bias
				    * current. Bit 7 of byte 78 is
				    * MSB, bit 0 of byte 79 is
				    * LSB. Tx_I(Offset) should be set
				    * to zero for “internally
				    * calibrated” devices. */
#define SFF_8472_TX_POWER_SLOPE	80 /* Tx_PWR(Slope) Fixed decimal
				    * (unsigned) calibration data,
				    * transmitter coupled output
				    * power. Bit 7 of byte 80 is MSB,
				    * bit 0 of byte 81 is LSB.
				    * Tx_PWR(Slope) should be set to 1
				    * for “internally calibrated”
				    * devices. */
#define SFF_8472_TX_POWER_OFFSET	82 /* Tx_PWR(Offset) Fixed decimal
				    * (signed two’s complement)
				    * calibration data, transmitter
				    * coupled output power. Bit 7 of
				    * byte 82 is MSB, bit 0 of byte 83
				    * is LSB. Tx_PWR(Offset) should be
				    * set to zero for “internally
				    * calibrated” devices. */
#define SFF_8472_T_SLOPE	84 /* T (Slope) Fixed decimal
				    * (unsigned) calibration data,
				    * internal module temperature. Bit
				    * 7 of byte 84 is MSB, bit 0 of
				    * byte 85 is LSB.  T(Slope) should
				    * be set to 1 for “internally
				    * calibrated” devices. */
#define SFF_8472_T_OFFSET	86 /* T (Offset) Fixed decimal (signed
				    * two’s complement) calibration
				    * data, internal module
				    * temperature. Bit 7 of byte 86 is
				    * MSB, bit 0 of byte 87 is LSB.
				    * T(Offset) should be set to zero
				    * for “internally calibrated”
				    * devices. */
#define SFF_8472_V_SLOPE	88 /* V (Slope) Fixed decimal
				    * (unsigned) calibration data,
				    * internal module supply
				    * voltage. Bit 7 of byte 88 is
				    * MSB, bit 0 of byte 89 is
				    * LSB. V(Slope) should be set to 1
				    * for “internally calibrated”
				    * devices. */
#define SFF_8472_V_OFFSET	90 /* V (Offset) Fixed decimal (signed
				    * two’s complement) calibration
				    * data, internal module supply
				    * voltage. Bit 7 of byte 90 is
				    * MSB. Bit 0 of byte 91 is
				    * LSB. V(Offset) should be set to
				    * zero for “internally calibrated”
				    * devices. */
#define SFF_8472_CHECKSUM	95 /* Checksum Byte 95 contains the
				    * low order 8 bits of the sum of
				    * bytes 0 – 94. */
 /* Internal measurements. */

#define SFF_8472_TEMP	 	96 /* Internally measured module temperature. */
#define SFF_8472_VCC 		98 /* Internally measured supply
				    * voltage in transceiver.
				    */
#define SFF_8472_TX_BIAS	100 /* Internally measured TX Bias Current. */
#define SFF_8472_TX_POWER	102 /* Measured TX output power. */
#define SFF_8472_RX_POWER	104 /* Measured RX input power. */

#define SFF_8472_STATUS		110 /* See below */

 /* Status Bits Described */

/*
 * TX Disable State Digital state of the TX Disable Input Pin. Updated
 * within 100ms of change on pin.
 */
#define SFF_8472_STATUS_TX_DISABLE  (1 << 7) 

/*
 * Select Read/write bit that allows software disable of
 * laser. Writing ‘1’ disables laser. See Table 3.11 for
 * enable/disable timing requirements. This bit is “OR”d with the hard
 * TX_DISABLE pin value. Note, per SFP MSA TX_DISABLE pin is default
 * enabled unless pulled low by hardware. If Soft TX Disable is not
 * implemented, the transceiver ignores the value of this bit. Default
 * power up value is zero/low.
 */
#define SFF_8472_STATUS_SOFT_TX_DISABLE (1 << 6) 

/*
 * RS(1) State Digital state of SFP input pin AS(1) per SFF-8079 or
 * RS(1) per SFF-8431. Updated within 100ms of change on pin. See A2h
 * Byte 118, Bit 3 for Soft RS(1) Select control information.
 */
#define SFF_8472_RS_STATE (1 << 5) 

/*
 * Rate_Select State [aka. “RS(0)”] Digital state of the SFP
 * Rate_Select Input Pin. Updated within 100ms of change on pin. Note:
 * This pin is also known as AS(0) in SFF-8079 and RS(0) in SFF-8431.
 */ 
#define SFF_8472_STATUS_SELECT_STATE (1 << 4)
     
/*
 * Read/write bit that allows software rate select control. Writing
 * ‘1’ selects full bandwidth operation. This bit is “OR’d with the
 * hard Rate_Select, AS(0) or RS(0) pin value. See Table 3.11 for
 * timing requirements. Default at power up is logic zero/low. If Soft
 * Rate Select is not implemented, the transceiver ignores the value
 * of this bit. Note: Specific transceiver behaviors of this bit are
 * identified in Table 3.6a and referenced documents. See Table 3.18a,
 * byte 118, bit 3 for Soft RS(1) Select.
 */
#define SFF_8472_STATUS_SOFT_RATE_SELECT (1 << 3)

/*
 * TX Fault State Digital state of the TX Fault Output Pin. Updated
 * within 100ms of change on pin.
 */
#define SFF_8472_STATUS_TX_FAULT_STATE (1 << 2)

/*
 * Digital state of the RX_LOS Output Pin. Updated within 100ms of
 * change on pin.
 */
#define SFF_8472_STATUS_RX_LOS (1 << 1)

/*
 * Indicates transceiver has achieved power up and data is ready. Bit
 * remains high until data is ready to be read at which time the
 * device sets the bit low.
 */
#define SFF_8472_STATUS_DATA_READY (1 << 0)

/* Table 3.2 Identifier values */
#define SFF_8472_ID_UNKNOWN	0x0 /* Unknown or unspecified */
#define SFF_8472_ID_GBIC	0x1 /* GBIC */
#define SFF_8472_ID_SFF		0x2 /* Module soldered to motherboard (ex: SFF)*/
#define SFF_8472_ID_SFP		0x3 /* SFP or SFP “Plus” */
#define SFF_8472_ID_XBI		0x4 /* Reserved for “300 pin XBI” devices */
#define SFF_8472_ID_XENPAK	0x5 /* Reserved for “Xenpak” devices */
#define SFF_8472_ID_XFP		0x6 /* Reserved for “XFP” devices */
#define SFF_8472_ID_XFF		0x7 /* Reserved for “XFF” devices */
#define SFF_8472_ID_XFPE	0x8 /* Reserved for “XFP-E” devices */
#define SFF_8472_ID_XPAK	0x9 /* Reserved for “XPak” devices */
#define SFF_8472_ID_X2		0xA /* Reserved for “X2” devices */
#define SFF_8472_ID_DWDM_SFP	0xB /* Reserved for “DWDM-SFP” devices */
#define SFF_8472_ID_QSFP	0xC /* Reserved for “QSFP” devices */
#define SFF_8472_ID_LAST	SFF_8472_ID_QSFP

static char *sff_8472_id[SFF_8472_ID_LAST + 1] = {"Unknown",
					     "GBIC",
					     "SFF",
					     "SFP",
					     "XBI",
					     "Xenpak",
					     "XFP",
					     "XFF",
					     "XFP-E",
					     "XPak",
					     "X2",
					     "DWDM-SFP",
					     "QSFP"};

/* Table 3.13 and 3.14 Temperature Conversion Values */
#define SFF_8472_TEMP_SIGN (1 << 15)
#define SFF_8472_TEMP_SHIFT  8
#define SFF_8472_TEMP_MSK  0xEF00
#define SFF_8472_TEMP_FRAC 0x00FF

/* Internal Callibration Conversion factors */

/*
 * Represented as a 16 bit unsigned integer with the voltage defined
 * as the full 16 bit value (0 – 65535) with LSB equal to 100 uVolt,
 * yielding a total range of 0 to +6.55 Volts.
 */
#define SFF_8472_VCC_FACTOR 10000.0 

/*
 * Represented as a 16 bit unsigned integer with the current defined
 * as the full 16 bit value (0 – 65535) with LSB equal to 2 uA,
 * yielding a total range of 0 to 131 mA.
 */

#define SFF_8472_BIAS_FACTOR 2000.0 

/*
 * Represented as a 16 bit unsigned integer with the power defined as
 * the full 16 bit value (0 – 65535) with LSB equal to 0.1 uW,
 * yielding a total range of 0 to 6.5535 mW (~ -40 to +8.2 dBm).
 */

#define SFF_8472_POWER_FACTOR 10000.0
