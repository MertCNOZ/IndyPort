// Lists the functions that use x87 transcendental instructions (FSIN, FCOS, FPTAN, FPATAN, FSQRT, FYL2X, F2XM1),
// with their asm, to find the CRT math helpers (_CIsin, _CIcos, ...) in .text.
// Usage (headless): -postScript FindCrtMath.java <out file>
// @category IndyET
import java.io.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class FindCrtMath extends GhidraScript {
    static final Set<String> OPS = new HashSet<>(Arrays.asList(
        "FSIN", "FCOS", "FSINCOS", "FPTAN", "FPATAN", "FSQRT", "FYL2X", "F2XM1", "FLDLN2", "FLDLG2"));

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "crt_math.txt");
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                List<String> asm = new ArrayList<>();
                Set<String> hit = new TreeSet<>();
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    String m = ins.getMnemonicString().toUpperCase();
                    if (OPS.contains(m)) hit.add(m);
                    asm.add("  " + ins.getAddress() + "  " + ins);
                }
                if (hit.isEmpty()) continue;
                w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses()
                    + " ops " + hit + " callers " + f.getCallingFunctions(monitor).size());
                if (asm.size() <= 120) asm.forEach(w::println);
                w.println();
            }
        }
        println("wrote " + out.getAbsolutePath());
    }
}
