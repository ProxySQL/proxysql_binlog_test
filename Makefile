#!/bin/make -f

.PHONY: default
default: test_binlog_reader-t

test_binlog_reader-t: test_binlog_reader-t.cpp libslave/libslave.a
	g++ -o test_binlog_reader-t test_binlog_reader-t.cpp -std=c++11 -ggdb ./libslave/libslave.a -I./libslave/ -L/usr/lib64/mysql -rdynamic -lz -ldl -lssl -lcrypto -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lssl -lcrypto -pthread

libslave/libslave.a:
	rm -rf libslave-20171226
	tar -zxf libslave-20171226.tar.gz
	# Enable for allowing other replication formats (STATEMENT, MIXED) for debugging purposes
	#patch -p0 < patches/slave_allow_rep_formats.patch
	patch -p0 < patches/libslave_DBUG_ASSERT.patch
	patch -p0 < patches/libslave_ER_MALFORMED_GTID_SET_ENCODING.patch
	patch -p0 < patches/libslave_SSL_MODE_DISABLED.patch
	patch -p0 < patches/libslave_MySQL_8_new_events.patch
	patch -p0 < patches/libslave_add_send_query_callback.patch
	cd libslave && cmake .
	cd libslave && make slave_a

.PHONY: build
build: build-ubuntu20

# universal distro target
.SILENT:
#.PHONY: $(filter $@,$(distros))
#$(filter $@,$(distros)): IMG_NAME=$(patsubst build-%,%,$@)
#$(filter $@,$(distros)):
.PHONY: build-%
build-%: IMG_NAME=$(patsubst build-%,%,$@)
build-%:
	echo 'building $@'
#	build in docker-compose.yml has templating bug, make the image here
	cd ./docker/build/ && ${MAKE} build-${IMG_NAME}
	IMG_NAME=$(IMG_NAME) docker-compose -p $(IMG_NAME) up mysqlbinlog
	docker-compose -p $(IMG_NAME) rm -f

.PHONY: cleanbuild
cleanbuild:
	rm -f test_binlog_reader-t || true
	rm -rf libslave-20171226
	find . -name '*.a' -delete
	find . -name '*.o' -delete

.PHONY: cleanall
cleanall: cleanbuild
	rm -rf binaries/*
