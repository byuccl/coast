DIRS = adpcm aes blowfish dfadd dfdiv dfmul dfsin gsm jpeg mips motion sha
LEVEL = ..

LLI = $(LEVEL)/../build/bin/lli

.PHONY: build run

build:
	for dir in $(DIRS); do \
		$(MAKE) exe -C $$dir || exit 1;\
	done

run: build
	@echo
	@echo -----------------------------------------------
	@echo Projects built, running checks on modified code
	@echo -----------------------------------------------
	@echo
	for dirAgain in $(DIRS); do \
		echo ; \
		echo ---;\
		echo $$dirAgain:;\
		$(MAKE) program -s -C $$dirAgain || exit 1; \
	done

clean:
	@rm -f */*.ll */*.bc */*.lbc


# $(LLI) ./$$dirAgain/*.ll || exit 1;
