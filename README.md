# BDO Ping Utility

C Win32API implementation of a TCP ping instead of the standard ICMP ping. Similar to tcpping utility that you can find in unix, and that Windows uses in Resource Manager->Network but does not expose under WinAPI32.

The goal was to be used with games that block ICMP pings to their server. In this case we have it configured for BDO but can be used for any game. 

# Usage

You must have BDO running for the program to retrieve game server ip.
Start it from command line with one argument, the amount of times it should ping before giving the average

# Compiling

Make sure to compile it as x86_64, char set should be set to Multi-Byte, compiles with vs2019
