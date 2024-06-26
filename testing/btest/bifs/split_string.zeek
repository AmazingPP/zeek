#
# @TEST-EXEC: zeek -b %INPUT >out
# @TEST-EXEC: btest-diff out

function print_string_vector(v: string_vec)
	{
	for ( i in v )
		print v[i];
	}

event zeek_init()
	{
	local a = "this is a test";
	local pat = /hi|es/;
	local idx = vector( 3, 6, 13);

	print_string_vector(split_string(a, pat));
	print "---------------------";
	print_string_vector(split_string1(a, pat));
	print "---------------------";
	print_string_vector(split_string_all(a, pat));
	print "---------------------";
	print_string_vector(split_string_n(a, pat, F, 1));
	print "---------------------";
	print_string_vector(split_string_n(a, pat, T, 1));
	print "---------------------";
	print str_split_indices(a, idx);
	print "---------------------";
	a = "X-Mailer: Testing Test (http://www.example.com)";
	pat = /:[[:blank:]]*/;
	print_string_vector(split_string1(a, pat));
	print "---------------------";
	a = "A = B = C = D";
	pat = /=/;
	print_string_vector(split_string_all(a, pat));
	}

event zeek_init() &priority=-5
	{
	# Anchor testing.
	local r = split_string_n("test", /^est/, T, 1);
	assert |r| == 1;
	assert r[0] == "test";
	print "test", "^est", r;

	r = split_string_n("test", /tes$/, T, 1);
	assert |r| == 1;
	assert r[0] == "test";
	print "test", "tes$", r;

	r = split_string_n("test", /^test$/, T, 1);
	assert |r| == 3;
	assert r[0] == "";
	assert r[1] == "test";
	assert r[2] == "";
	print "test", "^test$", r;

	r = split_string_n("aa bb cc", / ?/, F, 0);
	assert |r| == 3;
	assert r[0] == "aa";
	assert r[1] == "bb";
	assert r[2] == "cc";
	print "aa bb cc", "/ ?/", r;

	r = split_string_n("aa bb cc", / ?/, T, 0);
	assert |r| == 5;
	assert r[0] == "aa";
	assert r[1] == " ";
	assert r[2] == "bb";
	assert r[3] == " ";
	print "aa bb cc", "/ ?/", r;

	r = split_string_n("aa  bb    cc", / +/, F, 0);
	assert |r| == 3;
	assert r[0] == "aa";
	assert r[1] == "bb";
	assert r[2] == "cc";
	print "aa  bb   cc", "/ +/", r;
	}
