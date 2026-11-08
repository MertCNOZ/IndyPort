// Exports the library sections of the program for offline analysis:
//   functions.tsv  every function: entry, size, block, name, callers, callees
//   <block>.c      decompiled C of every function in the library blocks
// Usage (headless): -postScript ExportLibs.java <out dir>
// @category IndyET
import java.io.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;

public class ExportLibs extends GhidraScript {
    // xbe2pe writes one flat block, so the XBE library sections (tools/xbe_info.py) are listed by address.
    // Functions in them get decompiled; .text only goes to the table.
    static final String[] SECTION_NAMES = {"D3D", "D3DX", "XGRPH", "DSOUND", "WMADEC", "WMVDEC", "XPP"};
    static final long[][] SECTION_RANGES = {
        {0x498A60L, 0x4AB9F8L}, {0x4ABA00L, 0x4ADBE8L}, {0x4ADC00L, 0x4AFC18L}, {0x4AFC20L, 0x4CCBE4L},
        {0x4CCC00L, 0x4E5B38L}, {0x4E5B40L, 0x5183F4L}, {0x518400L, 0x51FF68L}};

    static String section(Address a) {
        long o = a.getOffset();
        for (int i = 0; i < SECTION_RANGES.length; i++)
            if (o >= SECTION_RANGES[i][0] && o < SECTION_RANGES[i][1]) return SECTION_NAMES[i];
        return o >= 0x11000L && o < 0x498A54L ? ".text" : "?";
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "export");
        out.mkdirs();

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Map<String, PrintWriter> cfiles = new HashMap<>();

        try (PrintWriter tsv = new PrintWriter(new FileWriter(new File(out, "functions.tsv")))) {
            tsv.println("entry\tsize\tblock\tname\tcallers\tcallees");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                Address entry = f.getEntryPoint();
                String block = section(entry);
                tsv.println(entry + "\t" + f.getBody().getNumAddresses() + "\t" + block + "\t" + f.getName()
                    + "\t" + names(f.getCallingFunctions(monitor)) + "\t" + names(f.getCalledFunctions(monitor)));

                if (block.equals(".text") || block.equals("?")) continue;
                PrintWriter c = cfiles.get(block);
                if (c == null) {
                    c = new PrintWriter(new FileWriter(new File(out, block.replace(".", "") + ".c")));
                    cfiles.put(block, c);
                }
                c.println("// ==== " + f.getName() + " @ " + entry + " size " + f.getBody().getNumAddresses()
                    + "\n// callers: " + names(f.getCallingFunctions(monitor)));
                DecompileResults r = dec.decompileFunction(f, 30, monitor);
                c.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
                // raw instructions too: offsets into structures are easiest to read there
                c.println("/* asm");
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    c.println("  " + ins.getAddress() + "  " + ins);
                }
                c.println("*/\n");
            }
        } finally {
            for (PrintWriter c : cfiles.values()) c.close();
            dec.dispose();
        }
        println("exported to " + out.getAbsolutePath());
    }

    static String names(Set<Function> fs) {
        List<String> l = new ArrayList<>();
        for (Function f : fs) l.add(f.getName() + "@" + f.getEntryPoint());
        Collections.sort(l);
        return String.join(",", l);
    }
}
