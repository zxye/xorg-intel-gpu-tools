=============
intel_gpu_top
=============

---------------------------------------------
Display a top-like summary of Intel GPU usage
---------------------------------------------
.. include:: defs.rst
:Author: IGT Developers <igt-dev@lists.freedesktop.org>
:Date: 2018-04-04
:Version: |PACKAGE_STRING|
:Copyright: 2009,2011,2012,2016,2018 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_gpu_top** [*OPTIONS*]

DESCRIPTION
===========

**intel_gpu_top** is a tool to display usage information on Intel GPU's.

The tool gathers data using perf performance counters (PMU) exposed by i915 and other platform drivers like RAPL (power) and Uncore IMC (memory bandwidth).

OPTIONS
=======

-s <ms>
    Refresh period in milliseconds.

-h
    Show help text.

LIMITATIONS
===========

* Not all metrics are supported on all platforms. Where a metric is unsupported it's value will be replaced by a dashed line.

* Non-root access to perf counters is controlled by the *perf_event_paranoid* sysctl.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
