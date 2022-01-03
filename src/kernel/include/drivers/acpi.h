#ifndef DRIVERS_ACPI_H
#define DRIVERS_ACPI_H

struct device;

bool acpi_attach_madt(struct device *device);

#endif