obj-m := cpqpen2.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc pentest.c -o pentest

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm ./pentest 2>&1 1>/dev/null
