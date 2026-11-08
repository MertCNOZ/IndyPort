// Writes the decompiled C and asm of the functions containing the given addresses, with callers and callees.
// Usage (headless): -postScript DumpAt.java <out file> <hex address> [<hex address> ...]
// @category IndyET
import java.io.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DumpAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> done = new HashSet<>();
        try (PrintWriter w = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    // code only reached indirectly: disassemble the bytes around it
                    w.println("// ==== no function at " + a + ", raw listing from " + a.subtract(0xC0));
                    Address s = a.subtract(0xC0);
                    disassemble(s);
                    InstructionIterator ii = currentProgram.getListing().getInstructions(s, true);
                    while (ii.hasNext()) {
                        Instruction ins = ii.next();
                        if (ins.getAddress().compareTo(a.add(0x40)) > 0) break;
                        w.println("  " + ins.getAddress() + "  " + ins);
                    }
                    w.println();
                    continue;
                }
                if (!done.add(f)) continue;
                w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses()
                    + " (asked " + a + ")");
                w.println("// callers: " + f.getCallingFunctions(monitor));
                w.println("// callees: " + f.getCalledFunctions(monitor));
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed");
                w.println("/* asm");
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) { Instruction ins = ii.next(); w.println("  " + ins.getAddress() + "  " + ins); }
                w.println("*/\n");
            }
        }
        dec.dispose();
    }
}
