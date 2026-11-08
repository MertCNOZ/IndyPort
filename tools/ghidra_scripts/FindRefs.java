// For each value: the instructions that use it (as an immediate or a memory address) and the places in memory
// that hold it as a 4-byte little-endian dword (pointers in tables, vtables).
// Usage (headless): -postScript FindRefs.java <out file> <hex value> [<hex value> ...]
// @category IndyET
import java.io.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;

public class FindRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                long v = Long.parseLong(args[i].replace("0x", ""), 16);
                w.println("==== " + Long.toHexString(v));
                InstructionIterator ii = currentProgram.getListing().getInstructions(true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    boolean hit = false;
                    for (int op = 0; op < ins.getNumOperands() && !hit; op++)
                        for (Object o : ins.getOpObjects(op)) {
                            if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == v) hit = true;
                            if (o instanceof Address && ((Address) o).getOffset() == v) hit = true;
                        }
                    if (hit) {
                        Function f = getFunctionContaining(ins.getAddress());
                        w.println("  code " + ins.getAddress() + "  " + ins + "   in " + (f == null ? "?" : f.getName()));
                    }
                }
                byte[] pat = {(byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24)};
                for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                    if (!b.isInitialized()) continue;
                    Address a = b.getStart();
                    while (a != null && a.compareTo(b.getEnd()) <= 0) {
                        Address hit = currentProgram.getMemory().findBytes(a, b.getEnd(), pat, null, true, monitor);
                        if (hit == null) break;
                        if (hit.getOffset() % 4 == 0) w.println("  data " + hit);
                        a = hit.add(1);
                    }
                }
            }
        }
    }
}
