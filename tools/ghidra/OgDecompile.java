// Decompiles the functions at the given addresses (script args) and prints the C output.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;

public class OgDecompile extends GhidraScript {
    public void run() throws Exception {
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        for (String a : getScriptArgs()) {
            Function f = getFunctionContaining(toAddr(a));
            if (f == null) { println("NOFUNC " + a); continue; }
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            println("=== " + f.getName() + " @ " + f.getEntryPoint() + "\n" + (r.getDecompiledFunction() == null ? r.getErrorMessage() : r.getDecompiledFunction().getC()));
        }
    }
}
