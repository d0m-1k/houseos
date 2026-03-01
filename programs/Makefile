SUBDIRS := fb_demo shell init

.PHONY: all clean stdlib $(SUBDIRS)

all: stdlib $(SUBDIRS)

stdlib:
	@$(MAKE) -C stdlib all

$(SUBDIRS): stdlib
	@$(MAKE) -C $@ all

clean:
	@$(MAKE) -C stdlib clean
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
