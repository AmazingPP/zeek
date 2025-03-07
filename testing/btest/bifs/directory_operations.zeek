#
# @TEST-EXEC: zeek -b %INPUT >out
# @TEST-EXEC: btest-diff out

event zeek_init()
	{
	# Test successful operations...
	print mkdir("testdir");
	print mkdir("testdir");
	local a = open("testdir/testfile");
	close(a);
	print rename("testdir/testfile", "testdir/testfile2");
	print rename("testdir", "testdir2");
	print unlink("testdir2/testfile2");
	print rmdir("testdir2");

	# ... and failing ones.
	print unlink("nonexisting");
	print rename("a", "b");
	print rmdir("nonexisting");
	a = open("testfile");
	close(a);
	print mkdir("testfile");
	}

event zeek_done()
	{
	# Only reached when above failures don't cause Zeek to exit.
	print "Shutting down.";
	}
