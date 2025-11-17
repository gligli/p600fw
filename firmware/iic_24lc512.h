#ifndef IIC_24LC512_H
#define IIC_24LC512_H

#define IIC_PAGE_SIZE 128

void iic_init(void);
void iic_send_byte(unsigned char addr_msb, unsigned char addr_lsb, unsigned char data);
void iic_send_page(unsigned char addr_msb, unsigned char addr_lsb, const unsigned char *data);
unsigned char iic_receive_byte(unsigned char addr_msb, unsigned char addr_lsb);
void iic_receive_page(unsigned char addr_msb, unsigned char addr_lsb, unsigned char *data);
void iic_test(void);

#endif /* IIC_24LC512_H */

