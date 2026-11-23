// Ghidra headless survey of the RO client for OdinGuard hook points.
// Prints callers of selected kernel32/winmm imports and functions referencing given strings.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import java.util.*;

public class OgSurvey extends GhidraScript {
    private void callers(String name) {
        SymbolTable st = currentProgram.getSymbolTable();
        for (Symbol s : st.getExternalSymbols(name)) {
            for (Reference r : getReferencesTo(s.getAddress())) {
                // references to the IAT slot
                for (Reference r2 : getReferencesTo(r.getFromAddress())) report(name, r2.getFromAddress());
                report(name, r.getFromAddress());
            }
        }
        for (Symbol s : st.getSymbols(name)) {
            for (Reference r : getReferencesTo(s.getAddress())) report(name, r.getFromAddress());
        }
    }
    private void report(String what, Address a) {
        Function f = getFunctionContaining(a);
        println(String.format("XREF %-22s from %s in %s", what, a, f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
    }
    private void strings(String needle) {
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            String v = String.valueOf(d.getValue());
            if (!v.equals(needle) && !(needle.endsWith("*") && v.startsWith(needle.substring(0, needle.length() - 1)))) continue;
            for (Reference r : getReferencesTo(d.getAddress())) report("str:" + v, r.getFromAddress());
        }
    }
    public void run() throws Exception {
        for (String n : new String[]{"Sleep", "timeGetTime", "QueryPerformanceCounter", "GetTickCount", "PeekMessageA", "GetProcAddress"}) callers(n);
        for (String s : getScriptArgs()) strings(s);
    }
}
