#ifndef DRIVERS_BUS_PCIPRIV_PCIID_H
#define DRIVERS_BUS_PCIPRIV_PCIID_H

#define PCI_GEN_CLASS(class, subclass) \
	(((subclass) << 8) | (class))

#define PCI_CLASS(class) \
	((class) & 0xFF)

#define PCI_SUBCLASS(class) \
	(((class) >> 8) & 0xFF)

#define PCI_DEFINE_CLASS(class, subclass) \
	PCI_GEN_CLASS(PCI_CLASS_ ## class, subclass)

#define PCI_CLASS_MASS_STORAGE			0x01
#define 	PCI_CLASS_IDE 				PCI_DEFINE_CLASS(MASS_STORAGE, 0x1)
#define PCI_CLASS_NETWORK			0x02
#define 	PCI_CLASS_NET_ETH			PCI_DEFINE_CLASS(NETWORK, 0x0)
#define PCI_CLASS_DISPLAY			0x03
#define 	PCI_CLASS_DISPLAY_VGA 			PCI_DEFINE_CLASS(DISPLAY, 0x00)
#define 	PCI_CLASS_DISPLAY_3D 			PCI_DEFINE_CLASS(DISPLAY, 0x02)
#define PCI_CLASS_MULTIMEDIA			0x04
#define PCI_CLASS_MEMORY			0x05
#define PCI_CLASS_BRIDGE			0x06
#define 	PCI_CLASS_BRIDGE_HOST			PCI_DEFINE_CLASS(BRIDGE, 0x00)
#define 	PCI_CLASS_BRIDGE_ISA			PCI_DEFINE_CLASS(BRIDGE, 0x01)
#define 	PCI_CLASS_BRIDGE_P2P			PCI_DEFINE_CLASS(BRIDGE, 0x04)
#define 	PCI_CLASS_BRIDGE_CARDBUS		PCI_DEFINE_CLASS(BRIDGE, 0x07)
#define PCI_CLASS_COMMUNICATION			0x07
#define PCI_CLASS_BASE_PERIPHERALS 		0x08
#define PCI_CLASS_INPUT 			0x09
#define PCI_CLASS_DOCKING_STATION		0x0A
#define PCI_CLASS_CPU				0x0B
#define PCI_CLASS_SERIAL_BUS 			0x0C
#define PCI_CLASS_WIRELESS			0x0D
#define PCI_CLASS_INTELLIGENT_IO 		0x0E
#define PCI_CLASS_SATELLITE_COMMUNICATION 	0x0F
#define PCI_CLASS_ENCRYPTION_DECRYPTION		0x10


#define PCI_ANY_ID 0xFFFF
#define PCI_ID(v, d, c) \
	(((uint64_t)((v) & 0xFFFF) << 0) | \
	 ((uint64_t)((d) & 0xFFFF) << 16) | \
	 ((uint64_t)((c) & 0xFFFF) << 32))

#define PCI_ID_VEN(id) (((id) >> 0)  & 0xFFFF)
#define PCI_ID_DEV(id) (((id) >> 16) & 0xFFFF)
#define PCI_ID_CLS(id) (((id) >> 32) & 0xFFFF)

/*
 * PCI device ids
 */
#define PCIV_CIRRUS		0x1013
#define 	PCI_ID_GD5446		0x00b8
#define PCIV_NVIDIA		0x10de
#define PCIV_QEMU		0x1234
#define		PCI_ID_QEMU_VGA           0x1111
#define PCIV_REDHAT		0x1af4
#define 	PCI_SUBSYS_QEMU		0x1100
#define PCIV_INTEL 		0x8086
#define		PCI_ID_82540EM_A	0x100e
#define 	PCI_ID_PIIX_ISA 	0x122E
#define 	PCI_ID_PIIX_IDE		0x1230
#define 	PCI_ID_82441FX		0x1237
#define 	PCI_ID_PIIX3_ISA	0x7000
#define 	PCI_ID_PIIX3_IDE	0x7010
#define 	PCI_ID_PIIX4_ISA	0x7110

#endif