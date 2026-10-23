RACK_DIR ?= ../../../Rack-SDK
.DEFAULT_GOAL := all
SOURCES += plugin.cpp SonavyreStudio.cpp
DISTRIBUTABLES += res LICENSE legal
.PHONY: product-resources
product-resources:
	python3 prepare_resources.py
all: product-resources
dist: product-resources
include $(RACK_DIR)/plugin.mk
