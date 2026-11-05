# Opt a recipe into strict Upstream-Status checking.

# Re-enable oe-core's patch-status check (needs the FB_ERROR_QA_REMOVE change in fb-base.inc)
FB_ERROR_QA_REMOVE:remove = "patch-status"

# Also require "Submitted [where]", same rule as patchtest
ERROR_QA:append = " patch-status-where"

python fb_qa_patch_status_where() {
    import re
    from oe import patch

    submitted_re = re.compile(r"^Upstream-Status: Submitted(.*)$", re.MULTILINE)
    where_re = re.compile(r" \[[^\]]*\S[^\]]*\]")
    for url in patch.src_patches(d):
        path = bb.fetch.decodeurl(url)[2]
        with open(path, encoding="utf-8", errors="ignore") as f:
            content = f.read()
        for m in submitted_re.finditer(content):
            if not where_re.match(m.group(1)):
                oe.qa.handle_error("patch-status-where",
                    "Upstream-Status is Submitted, but it is not mentioned where:\n%s\n%s\n"
                    "Use: Upstream-Status: Submitted [<gerrit/lore URL>]" % (path, m.group(0).strip()), d)
    oe.qa.exit_if_errors(d)
}
do_patch[postfuncs] += "fb_qa_patch_status_where "
# More checking logic to come here later
