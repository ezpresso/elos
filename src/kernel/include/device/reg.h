#ifndef DEVICE_REG_H
#define DEVICE_REG_H

#define REG(addr, width) (addr, width)
#define __REG_ADDR(addr, width) addr
#define __REG_WIDTH(addr, width) width
#define REG_ADDR(reg) __REG_ADDR reg
#define REG_WIDTH(reg) __REG_WIDTH reg
#define REG_TYPE(reg) CONCAT(CONCAT(uint, REG_WIDTH(reg)), _t)

#define FIELD(hibit, lobit) (hibit, lobit)
#define FIELD_BIT(bit) FIELD(bit, bit)
#define __FIELD_LOBIT(hibit, lobit) lobit
#define __FIELD_HIBIT(hibit, lobit) hibit
#define _FIELD_LOBIT(field) __FIELD_LOBIT field
#define _FIELD_HIBIT(field) __FIELD_HIBIT field
#define _FIELD_WIDTH(field) (1 + _FIELD_HIBIT(field) - _FIELD_LOBIT(field))
#define _FIELD_MASK(field) ((1ULL << _FIELD_WIDTH(field)) - 1)

#define _FIELD_GET(R, field, g) (REG_TYPE(R)) \
	(((REG_TYPE(R))(g) >> _FIELD_LOBIT(field)) & _FIELD_MASK(field))

#define _FIELD_MAKEI(R, field, val) \
	(((REG_TYPE(R))((val) & _FIELD_MASK(field))) << _FIELD_LOBIT(field))

#define FIELD_GET(R, F, g) _FIELD_GET(R, R##_##F, g)
#define FIELD_MAKEI(R, F, val) _FIELD_MAKEI(R, R##_##F, val)
#define FIELD_MAKE(R, F, V) _FIELD_MAKEI(R, R##_##F, R##_##F##_##V)
#define FIELD_TEST(R, F, g, V) (_FIELD_GET(R, R##_##F, g) == R##_##F##_##V)

#define bus_reg_read(res, reg) \
	CONCAT(bus_res_read, REG_WIDTH(reg)) (res, REG_ADDR(reg))

#define bus_reg_get_field(res, R, F) \
	_FIELD_GET(R, R##_##F, bus_reg_read(res, R))

#define bus_reg_write(res, reg, val) \
	CONCAT(bus_res_write, REG_WIDTH(reg)) (res, REG_ADDR(reg), val)

#endif