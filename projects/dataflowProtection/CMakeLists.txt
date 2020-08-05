cmake_minimum_required(VERSION 3.5)


add_llvm_loadable_module(DataflowProtection
	dataflowProtection.cpp
	cloning.cpp
	synchronization.cpp
	utils.cpp
    verification.cpp
    interface.cpp
    inspection.cpp
	dataflowProtection.h
)
