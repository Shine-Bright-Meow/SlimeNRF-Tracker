#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# NORDIC SDK APP START
FILE(GLOB_RECURSE app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# NORDIC SDK APP END

FILE(GLOB_RECURSE files Fusion/Fusion/*.c)
target_sources(app PRIVATE ${files})

FILE(GLOB_RECURSE files vqf-c/src/*.c)
target_sources(app PRIVATE ${files})
