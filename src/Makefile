# redirect all targets to toplevel

all:
	@cd ..; make

ifneq ($(MAKECMDGOALS),)
ifneq ($(MAKECMDGOALS),all)
$(MAKECMDGOALS):
	@cd ..; make $(MAKECMDGOALS)
endif
endif
