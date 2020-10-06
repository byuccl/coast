
build:
	cd projects/build && cmake .. && make

test_fast:
	python3 unittest/unittest.py unittest/cfg/fast.yml

test_full_tmr:
	python3 unittest/unittest.py unittest/cfg/full_tmr.yml

test_full:
	python3 unittest/unittest.py unittest/cfg/full.yml

# runs COAST on the unit tests
test_regression:
	python3 unittest/pyDriver.py unittest/cfg/regression.yml

# ensures that all RTOS benchmarks compile and run correctly
test_rtos:
	./unittest/rtos_test.sh

.PHONY: build
