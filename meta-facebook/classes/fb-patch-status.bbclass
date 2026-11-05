# Opt a recipe into strict Upstream-Status checking.

# Re-enable oe-core's patch-status check (needs the FB_ERROR_QA_REMOVE change in fb-base.inc)
FB_ERROR_QA_REMOVE:remove = "patch-status"

# More checking logic to come here later
