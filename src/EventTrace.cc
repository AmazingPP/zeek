// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/Desc.h"
#include "zeek/EventTrace.h"
#include "zeek/IPAddr.h"
#include "zeek/Reporter.h"
#include "zeek/ZeekString.h"


namespace zeek::detail
	{

ValTrace::ValTrace(const ValPtr& _v)
	{
	v = _v;
	t = v->GetType();

	switch ( t->Tag() )
		{
		case TYPE_LIST:
			TraceList(cast_intrusive<ListVal>(v));
			break;

		case TYPE_RECORD:
			TraceRecord(cast_intrusive<RecordVal>(v));
			break;

		case TYPE_TABLE:
			TraceTable(cast_intrusive<TableVal>(v));
			break;

		case TYPE_VECTOR:
			TraceVector(cast_intrusive<VectorVal>(v));
			break;

		default:
			break;
		}
	}

ValTrace::~ValTrace()
	{
	}

bool ValTrace::operator==(const ValTrace& vt) const
	{
	auto& vt_v = vt.GetVal();
	if ( vt_v == v )
		return true;

	auto tag = t->Tag();

	if ( vt.GetType()->Tag() != tag )
		return false;

	switch ( tag )
		{
		case TYPE_BOOL:
		case TYPE_INT:
		case TYPE_ENUM:
			return v->AsInt() == vt_v->AsInt();

		case TYPE_COUNT:
		case TYPE_PORT:
			return v->AsCount() == vt_v->AsCount();

		case TYPE_DOUBLE:
		case TYPE_INTERVAL:
		case TYPE_TIME:
			return v->AsDouble() == vt_v->AsDouble();

		case TYPE_STRING:
			return (*v->AsString()) == (*vt_v->AsString());

		case TYPE_ADDR:
			return v->AsAddr() == vt_v->AsAddr();

		case TYPE_SUBNET:
			return v->AsSubNet() == vt_v->AsSubNet();

		case TYPE_FUNC:
			return v->AsFile() == vt_v->AsFile();

		case TYPE_FILE:
			return v->AsFile() == vt_v->AsFile();

		case TYPE_PATTERN:
			return v->AsPattern() == vt_v->AsPattern();

		case TYPE_ANY:
			return v->AsSubNet() == vt_v->AsSubNet();

		case TYPE_TYPE:
			return v->AsType() == vt_v->AsType();

		case TYPE_OPAQUE:
			return false; // needs pointer equivalence

		case TYPE_LIST:
			return SameList(vt);

		case TYPE_RECORD:
			return SameRecord(vt);

		case TYPE_TABLE:
			return SameTable(vt);

		case TYPE_VECTOR:
			return SameVector(vt);

		default:
			reporter->InternalError("bad type in ValTrace::operator==");
		}
	}

void ValTrace::ComputeDelta(const ValTrace* prev, DeltaVector& deltas) const
	{
	auto tag = t->Tag();

	if ( prev )
		{
		ASSERT(prev->GetType()->Tag() == tag);

		auto& prev_v = prev->GetVal();

		if ( prev_v != v )
			{
			if ( *this != *prev )
				deltas.emplace_back(std::make_unique<DeltaReplaceValue>(this, v));
			return;
			}
		}

	switch ( tag )
		{
		case TYPE_BOOL:
		case TYPE_INT:
		case TYPE_ENUM:
		case TYPE_COUNT:
		case TYPE_PORT:
		case TYPE_DOUBLE:
		case TYPE_INTERVAL:
		case TYPE_TIME:
		case TYPE_STRING:
		case TYPE_ADDR:
		case TYPE_SUBNET:
		case TYPE_FUNC:
		case TYPE_PATTERN:
		case TYPE_TYPE:
			// These don't change in place.
			if ( ! prev )
				deltas.emplace_back(std::make_unique<DeltaReplaceValue>(this, v));
			break;

		case TYPE_FILE:
		case TYPE_OPAQUE:
		case TYPE_ANY:
			// These we have no way of creating as constants.
			reporter->Error("cannot generate an event trace for an event of type %s", type_name(tag));
			break;

		case TYPE_LIST:
			// We shouldn't see these exposed directly, as
			// they're not manipulable at script-level.  An
			// exception might be for "any" types that are
			// then decomposed via compound assignment; for
			// now, we don't support those.
			reporter->InternalError("list type seen in ValTrace::ComputeDelta");
			break;

		case TYPE_RECORD:
			if ( prev )
				ComputeRecordDelta(prev, deltas);
			else
				deltas.emplace_back(std::make_unique<DeltaRecordCreate>(this));
			break;

		case TYPE_TABLE:
			if ( prev )
				ComputeTableDelta(prev, deltas);

			else if ( t->Yield() )
				deltas.emplace_back(std::make_unique<DeltaTableCreate>(this));
			else
				deltas.emplace_back(std::make_unique<DeltaSetCreate>(this));
			break;

		case TYPE_VECTOR:
			if ( prev )
				ComputeVectorDelta(prev, deltas);
			else
				deltas.emplace_back(std::make_unique<DeltaVectorCreate>(this));
			break;

		default:
			reporter->InternalError("bad type in ValTrace::ComputeDelta");
		}
	}

void ValTrace::Dump(int indent_level) const
	{
	Indent(indent_level);

	if ( IsAggr(t) || t->Tag() == TYPE_LIST )
		{
		bool is_table = ! elems2.empty();

		printf("aggregate of type \"%s\" with %lu elements:\n", t->GetName().c_str(), elems.size());

		for ( auto i = 0; i < elems.size(); ++i )
			{
			auto& e = elems[i];
			if ( e )
				{
				e->Dump(indent_level + 1);
				if ( is_table )
					elems2[i]->Dump(indent_level + 2);
				}
			else
				{
				Indent(indent_level + 1);
				printf("<nil>\n");
				}
			}
		}

	else
		{
		ODesc d;
		v->Describe(&d);
		printf("singleton %s\n", d.Description());
		}
	}

void ValTrace::TraceList(const ListValPtr& lv)
	{
	auto vals = lv->Vals();
	for ( auto& v : vals )
		elems.emplace_back(std::make_shared<ValTrace>(v));
	}

void ValTrace::TraceRecord(const RecordValPtr& rv)
	{
	auto n = rv->NumFields();
	auto rt = rv->GetType<RecordType>();

	for ( auto i = 0U; i < n; ++i )
		{
		auto f = rv->RawOptField(i);
		if ( f )
			{
			auto val = f->ToVal(rt->GetFieldType(i));
			elems.emplace_back(std::make_shared<ValTrace>(val));
			}
		else
			elems.emplace_back(nullptr);
		}
	}

void ValTrace::TraceTable(const TableValPtr& tv)
	{
	for ( auto& elem : tv->ToMap() )
		{
		auto& key = elem.first;
		elems.emplace_back(std::make_shared<ValTrace>(key));

		auto& val = elem.second;
		if ( val )
			elems2.emplace_back(std::make_shared<ValTrace>(val));
		}
	}

void ValTrace::TraceVector(const VectorValPtr& vv)
	{
	auto& vec = vv->RawVec();
	auto n = vec->size();
	auto& yt = vv->RawYieldType();
	auto& yts = vv->RawYieldTypes();

	for ( auto i = 0U; i < n; ++i )
		{
		auto& elem = (*vec)[i];
		if ( elem )
			{
			auto& t = yts ? (*yts)[i] : yt;
			auto val = elem->ToVal(t);
			elems.emplace_back(std::make_shared<ValTrace>(val));
			}
		else
			elems.emplace_back(nullptr);
		}
	}

bool ValTrace::SameList(const ValTrace& vt) const
	{
	return SameElems(vt);
	}

bool ValTrace::SameRecord(const ValTrace& vt) const
	{
	return SameElems(vt);
	}

bool ValTrace::SameTable(const ValTrace& vt) const
	{
	auto& vt_elems = vt.elems;
	auto n = elems.size();
	if ( n != vt_elems.size() )
		return false;

	auto& vt_elems2 = vt.elems2;
	auto n2 = elems2.size();
	if ( n2 != vt_elems2.size() )
		return false;

	ASSERT(n2 == 0 || n == n2);

	// We accommodate the possibility that keys are out-of-order
	// between the two sets of elements.

	// The following is O(N^2), but presumably if tables are somehow
	// involved (in fact we can only get here if they're used as
	// indices into other tables), then they'll likely be small.
	for ( auto i = 0U; i < n; ++i )
		{
		auto& elem_i = elems[i];

		// See if we can find a match for it.  If we do, we don't
		// have to worry that another entry matched it too, since
		// all table/set indices will be distinct.
		auto j = 0U;
		for ( ; j < n; ++j )
			{
			auto& vt_elem_j = vt_elems[j];
			if ( *elem_i == *vt_elem_j )
				break;
			}

		if ( j == n )
			// No match for the index.
			return false;

		if ( n2 > 0 )
			{
			// Need a match for the corresponding yield values.
			if ( *elems2[i] != *vt_elems2[j] )
				return false;
			}
		}

	return true;
	}

bool ValTrace::SameVector(const ValTrace& vt) const
	{
	return SameElems(vt);
	}

bool ValTrace::SameElems(const ValTrace& vt) const
	{
	auto& vt_elems = vt.elems;
	auto n = elems.size();
	if ( n != vt_elems.size() )
		return false;

	for ( auto i = 0U; i < n; ++i )
		{
		auto& trace_i = elems[i];
		auto& vt_trace_i = vt_elems[i];

		if ( trace_i && vt_trace_i )
			{
			if ( *trace_i != *vt_trace_i )
				return false;
			}

		else if ( trace_i || vt_trace_i )
			return false;
		}

	return true;
	}

void ValTrace::ComputeRecordDelta(const ValTrace* prev, DeltaVector& deltas) const
	{
	auto& prev_elems = prev->elems;
	auto n = elems.size();
	if ( n != prev_elems.size() )
		reporter->InternalError("size inconsistency in ValTrace::ComputeRecordDelta");

	for ( auto i = 0U; i < n; ++i )
		{
		const auto trace_i = elems[i].get();
		const auto prev_trace_i = prev_elems[i].get();

		if ( trace_i )
			{
			if ( prev_trace_i )
				{
				auto& v = trace_i->GetVal();
				auto& prev_v = prev_trace_i->GetVal();

				if ( v == prev_v )
					{
					trace_i->ComputeDelta(prev_trace_i, deltas);
					continue;
					}
				}

			deltas.emplace_back(std::make_unique<DeltaSetField>(this, i, trace_i->GetVal()));
			}

		else if ( prev_trace_i )
			deltas.emplace_back(std::make_unique<DeltaSetField>(this, i, nullptr));
		}
	}

void ValTrace::ComputeTableDelta(const ValTrace* prev, DeltaVector& deltas) const
	{
	auto& prev_elems = prev->elems;
	auto& prev_elems2 = prev->elems2;

	auto n = elems.size();
	auto is_set = elems2.size() == 0;
	auto prev_n = prev_elems.size();

	// We can't compare pointers for the indices because they're
	// new objects generated afresh by TableVal::ToMap.  So we do
	// explict full comparisons for equality, distinguishing values
	// newly added, common to both, or (implicitly) removed.  We'll
	// then go through the common to check them further.
	//
	// Our approach is O(N^2), but presumably these tables aren't
	// large, and in any case generating event traces is not something
	// requiring high performance, so we opt for conceptual simplicity.

	// Track which index values are newly added:
	std::set<const Val*> added_indices;

	// Track which entry traces are in common.  Indexed by previous
	// trace elem index, yielding current trace elem index.
	std::map<int, int> common_entries;

	for ( auto i = 0U; i < n; ++i )
		{
		const auto trace_i = elems[i].get();

		bool common = false;

		for ( auto j = 0U; j < prev_n; ++j )
			{
			const auto prev_trace_j = prev_elems[j].get();

			if ( *trace_i == *prev_trace_j )
				{
				common_entries[j] = i;
				common = true;
				break;
				}
			}

		if ( ! common )
			{
			auto v = trace_i->GetVal();

			if ( is_set )
				deltas.emplace_back(std::make_unique<DeltaSetSetEntry>(this, v));
			else
				{
				auto yield = elems2[i]->GetVal();
				deltas.emplace_back(std::make_unique<DeltaSetTableEntry>(this, v, yield));
				}

			added_indices.insert(v.get());
			}
		}

	for ( auto j = 0U; j < prev_n; ++j )
		{
		auto common_pair = common_entries.find(j);
		if ( common_pair == common_entries.end() )
			{
			auto& prev_trace = prev_elems[j];
			auto& v = prev_trace->GetVal();
			deltas.emplace_back(std::make_unique<DeltaRemoveTableEntry>(this, v));
			continue;
			}

		if ( is_set )
			continue;

		// If we get here, we're analyzing a table for which there's
		// a common index.  The remaining question is whether the
		// yield has changed.
		auto i = common_pair->second;
		auto& trace2 = elems2[i];
		const auto prev_trace2 = prev_elems2[j].get();

		auto& yield = trace2->GetVal();
		auto& prev_yield = prev_trace2->GetVal();

		if ( yield == prev_yield )
			// Same yield, look for differences in its sub-elements.
			trace2->ComputeDelta(prev_trace2, deltas);
		else
			deltas.emplace_back(std::make_unique<DeltaSetTableEntry>(this, elems[i]->GetVal(), yield));
		}
	}

void ValTrace::ComputeVectorDelta(const ValTrace* prev, DeltaVector& deltas) const
	{
	auto& prev_elems = prev->elems;
	auto n = elems.size();
	auto prev_n = prev_elems.size();

	if ( n < prev_n )
		{
		// The vector shrank in size.  Easiest to just build it
		// from scratch.
		deltas.emplace_back(std::make_unique<DeltaVectorCreate>(this));
		return;
		}

	// Look for existing entries that need reassigment.
	auto i = 0U;
	for ( ; i < prev_n; ++i )
		{
		const auto trace_i = elems[i].get();
		const auto prev_trace_i = prev_elems[i].get();

		auto& elem_i = trace_i->GetVal();
		auto& prev_elem_i = prev_trace_i->GetVal();

		if ( elem_i == prev_elem_i )
			trace_i->ComputeDelta(prev_trace_i, deltas);
		else
			deltas.emplace_back(std::make_unique<DeltaVectorSet>(this, i, elem_i));
		}

	// Now append any new entries.
	for ( ; i < n; ++i )
		{
		auto& trace_i = elems[i];
		auto& elem_i = trace_i->GetVal();
		deltas.emplace_back(std::make_unique<DeltaVectorAppend>(this, i, elem_i));
		}
	}

void ValTrace::Indent(int indent_level) const
	{
	for ( auto i = 0; i < indent_level; ++i )
		printf("\t");
	}

std::string ValDelta::Generate(ValTraceMgr* vtm) const
	{
	return "<bad ValDelta>";
	}

std::string ValDelta::ValDesc(const ValPtr& v) const
	{
	ODesc d;
	v->Describe(&d);
	return d.Description();
	}

std::string DeltaReplaceValue::Generate(ValTraceMgr* vtm) const
	{
	return std::string(" = ") + vtm->ValName(new_val);
	}

std::string DeltaSetField::Generate(ValTraceMgr* vtm) const
	{
	auto rt = vt->GetType()->AsRecordType();
	auto f = rt->FieldName(field);
	return std::string("$") + f + " = " + vtm->ValName(new_val);
	}

std::string DeltaRecordCreate::Generate(ValTraceMgr* vtm) const
	{
	auto rv = cast_intrusive<RecordVal>(vt->GetVal());
	auto rt = rv->GetType<RecordType>();
	auto n = rt->NumFields();

	std::string args;

	for ( auto i = 0; i < n; ++i )
		{
		auto v_i = rv->GetField(i);
		if ( v_i )
			{
			if ( ! args.empty() )
				args += ", ";

			args += std::string("$") + rt->FieldName(i) + "=" + vtm->ValName(v_i);
			}
		}

	auto name = rt->GetName();
	if ( name.empty() )
		name = "record";

	return std::string(" = ") + name + "(" + args + ")";
	}

std::string DeltaSetSetEntry::Generate(ValTraceMgr* vtm) const
	{
	return std::string("add ") + vtm->ValName(vt) + "[" + vtm->ValName(index) + "]";
	}

std::string DeltaSetTableEntry::Generate(ValTraceMgr* vtm) const
	{
	return std::string("[") + vtm->ValName(index) + "] = " + vtm->ValName(new_val);
	}

std::string DeltaRemoveTableEntry::Generate(ValTraceMgr* vtm) const
	{
	return std::string("delete ") + vtm->ValName(vt) + "[" + vtm->ValName(index) + "]";
	}

std::string DeltaSetCreate::Generate(ValTraceMgr* vtm) const
	{
	auto sv = cast_intrusive<TableVal>(vt->GetVal());
	auto members = sv->ToMap();

	std::string args;

	for ( auto& m : members )
		{
		if ( ! args.empty() )
			args += ", ";

		args += vtm->ValName(m.first);
		}

	auto name = sv->GetType()->GetName();
	if ( name.empty() )
		name = "set";

	return std::string(" = ") + name + "(" + args + ")";
	}

std::string DeltaTableCreate::Generate(ValTraceMgr* vtm) const
	{
	auto tv = cast_intrusive<TableVal>(vt->GetVal());
	auto members = tv->ToMap();

	std::string args;

	for ( auto& m : members )
		{
		if ( ! args.empty() )
			args += ", ";

		args += std::string("[") + vtm->ValName(m.first) + "] = " + vtm->ValName(m.second);
		}

	auto name = tv->GetType()->GetName();
	if ( name.empty() )
		name = "table";

	return std::string(" = ") + name + "(" + args + ")";
	}

std::string DeltaVectorSet::Generate(ValTraceMgr* vtm) const
	{
	return std::string("[") + std::to_string(index) + "] = " + vtm->ValName(elem);
	}

std::string DeltaVectorAppend::Generate(ValTraceMgr* vtm) const
	{
	return std::string("[") + std::to_string(index) + "] = " + vtm->ValName(elem);
	}

std::string DeltaVectorCreate::Generate(ValTraceMgr* vtm) const
	{
	auto& elems = vt->GetElems();
	std::string vec;

	for ( auto& e : elems )
		{
		if ( vec.size() > 0 )
			vec += ", ";

		vec += vtm->ValName(e->GetVal());
		}

	return std::string(" = vector(") + vec + ")";
	}

void ValTraceMgr::AddVal(ValPtr v)
	{
	auto mapping = val_map.find(v.get());

	if ( mapping == val_map.end() )
		NewVal(v);
	else
		{
		auto vt = std::make_shared<ValTrace>(v);
		AssessChange(vt.get(), mapping->second.get());
		val_map[vt->GetVal().get()] = vt;
		}
	}

const std::string& ValTraceMgr::ValName(const ValPtr& v)
	{
	auto find = val_names.find(v.get());
	if ( find == val_names.end() )
		{
		if ( IsAggr(v->GetType()) )
			{ // Aggregate shouldn't exist; create it
			ASSERT(val_map.count(v.get()) == 0);
			NewVal(v);
			find = val_names.find(v.get());
			}

		else
			{ // Non-aggregate can be expressed using a constant
			ODesc d;
			v->Describe(&d);
			val_names[v.get()] = d.Description();
			find = val_names.find(v.get());
			}

		ASSERT(find != val_names.end());
		}

	return find->second;
	}

void ValTraceMgr::NewVal(ValPtr v)
	{
	// Make sure the Val sticks around into the future.
	vals.push_back(v);

	auto vt = std::make_shared<ValTrace>(v);
	AssessChange(vt.get(), nullptr);
	val_map[v.get()] = vt;
	}

void ValTraceMgr::AssessChange(const ValTrace* vt, const ValTrace* prev_vt)
	{
	DeltaVector deltas;

	vt->ComputeDelta(prev_vt, deltas);

	printf("reuse of %llx, %lu differences\n", vt->GetVal().get(), deltas.size());

	for ( auto i = 0U; i < deltas.size(); ++i )
		ProcessDelta(deltas[i].get());
	}

void ValTraceMgr::ProcessDelta(const ValDelta* d)
	{
	auto gen = d->Generate(this);

	if ( d->NeedsLHS() )
		{
		auto v = d->GetValTrace()->GetVal().get();
		if ( val_names.count(v) == 0 )
			TrackVar(v);

		gen = val_names[v] + gen;
		}

	printf("\t%s\n", gen.c_str());
	}

void ValTraceMgr::TrackVar(const Val* v)
	{
	auto val_name = std::string("__val") + std::to_string(num_vars++);
	val_names[v] = val_name;
	}

	} // namespace zeek::detail