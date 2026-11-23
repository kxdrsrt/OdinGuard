// Prints the first N instructions of the functions at the given addresses and their RET instructions.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class OgAsm extends GhidraScript {
    public void run() throws Exception {
        for (String a : getScriptArgs()) {
            Function f = getFunctionContaining(toAddr(a));
            println("=== " + (f == null ? a : f.getName() + " cc=" + f.getCallingConventionName() + " params=" + f.getParameterCount()));
            InstructionIterator it = currentProgram.getListing().getInstructions(toAddr(a), true);
            int n = 0;
            while (it.hasNext() && n < 400) {
                Instruction i = it.next();
                if (f != null && !f.getBody().contains(i.getAddress())) break;
                if (n < 25 || i.getMnemonicString().startsWith("RET")) println(i.getAddress() + "  " + i);
                n++;
            }
        }
    }
}
