#!/bin/bash

mkdir -p data
cp -r ../bios data/bios
cp -r ../build/disk.img data/disk.img
docker-compose run main bash -c "sudo chmod 0777 /dev/kvm && exec qemu-system-x86_64 -s -drive file=/home/developer/data/disk.img -bios /home/developer/data/bios/OVMF.fd -enable-kvm -nic none"
