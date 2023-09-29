@echo off
tools\atxt32 -l@@ 8080exe.asm 8080exe.mac
tools\zmac -8 -m --oo cim 8080exe.mac

tools\atxt32 -l@@ 8080pre.asm 8080pre.mac
tools\zmac -8 -m --oo cim 8080pre.mac

del 8080exe.mac
del 8080pre.mac


