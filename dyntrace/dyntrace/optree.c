/*
 * Copyright (c) 2004 Kelly Yancey
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $kbyanc: dyntrace/dyntrace/optree.c,v 1.13 2004/12/27 04:32:44 kbyanc Exp $
 */

#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#if defined(__FreeBSD__) && __FreeBSD__ >= 5
#include <arpa/inet.h>	/* for htonl() */
#endif

#include "dyntrace.h"
#include "radix.h"

/*!
 * @file
 *
 * We use the same radix tree code that FreeBSD (and other 4.4BSD derivatives)
 * uses for routing lookups to implement opcode identification.  This data
 * structure is perfectly suited for matching opcode bit strings as it provides
 * best-match lookups with masking.  Masking is especially useful as it allows
 * for don't-care bits in opcode bit strings (a requirement for the x86
 * instruction set and possibly others).
 *
 * For an explanation of how the radix tree works, see:
 * Gary R. Wright and W. Richard Stevens. TCP/IP Illustrated, Volume 2:
 * The Implementation, chapter 18.
 *
 */


/*!
 * @struct bitval
 *
 * Data structure representing a string of bits up to 32 bits long.  Used as a
 * key for radix tree lookups so the first byte must include the length of the
 * structure in bytes (simulating a BSD sockaddr structure for which the radix
 * code was originally designed).  Furthermore, the bit string itself is
 * aligned on a word boundary to improve performance.  There will likely be
 * compiler-added padding between the len and val members.
 *
 *	@param	len		Length of the structure in bytes. 
 *				Same as sizeof(struct bitval).
 *
 *	@param	val		Storage for bit string.  Aligned to word
 *				boundary to allow for fast word-sized access.
 */
struct bitval {
	uint8_t		 len;
	uint32_t	 val;		/* XXX Should be uint_fast32_t. */
};


/*!
 * @struct OpTreeNode
 *
 * Data structure representing a single opcode.  This is used as an entry in
 * the radix tree so the first 2 fields must be pointers to radix tree nodes
 * (simulating a BSD rtentry structure).
 */
struct OpTreeNode {
	struct radix_node rn[2];
	struct bitval	 match; 
	struct bitval	 mask;
	enum { OPCODE, PREFIX } type;
};


typedef uint prefixmask_t;
#define	PREFIXMASK_EMPTY	0
#define	MAX_PREFIXES		(sizeof(prefixmask_t) * 8)

struct Prefix {
	struct OpTreeNode node;

	uint8_t		 len;
	uint8_t		 id;
	prefixmask_t	 mask;
	char		*bitmask;
	char		*detail;
};


/*!
 * @struct counter
 *
 *	Each opcode has a list of counters per memory region type.  Each
 *	counter in the list represents the usage count and timing for the
 *	opcode with a given set of prefixes.  Since the most common case
 *	is an opcode unadorned with prefix bytes, the first counter in the
 *	list is embedded within the opcode structure itself and has a nul
 *	prefix mask.
 *
 *	@param	next		Pointer to next counter in list.
 *
 *	@param	prefixmask	Prefix mask this counter is for.
 *
 *	@param	count		The number of times the opcode has been
 *				executed with our list of prefixes.
 *
 *	@param	cycles_total	The total number of CPU cycles accumulated
 *				across all executions.
 *
 *	@param	cycles_min	The minimum number of CPU cycles for any
 *				single execution.
 *
 *	@param	cycles_max	The maximum number of CPU cycles for any
 *				single execution.
 */
struct counter {
	struct counter	*next;
	prefixmask_t	 prefixmask;

	uint64_t	 n;
	uint64_t	 cycles_total;
	uint		 cycles_min;
	uint		 cycles_max;
};


/*!
 * @struct Opcode
 */
struct Opcode {
	struct OpTreeNode node;

	struct counter	 count_head[NUMREGIONTYPES];
	struct counter	*count_end[NUMREGIONTYPES];

	char		*bitmask;
	char		*mneumonic;
	char		*detail;
};


static struct radix_node_head *op_rnh = NULL;
static struct Prefix prefix_index[MAX_PREFIXES];
static uint	 prefix_count = 0;
static xmlTextWriterPtr writer = NULL;
static int	 writer_fd = -1;
static bool	 region_type_use[NUMREGIONTYPES];


static void	 optree_init(void);
static bool	 optree_insert(struct OpTreeNode *op);
static struct OpTreeNode *optree_lookup(const void *keyptr);
static int	 optree_print_node(struct radix_node *rn, void *arg);

static struct Opcode *opcode_alloc(void);
static void	 opcode_parse(xmlNode *node);
static void	 opcode_free(struct Opcode **opp);

static const char *prefix_string(prefixmask_t prefixmask);
static void	 prefix_parse(xmlNode *node);
static void	 prefix_free(struct Prefix **prefixp);

static void	 parse_bitmask(const char *bitstr,
			       uint32_t *mask, uint32_t *match);



/*!
 * optree_init() - Initialize radix tree routines for use as opcode lookup tree.
 */
void
optree_init(void)
{
	struct Opcode *op;

	assert(op_rnh == NULL);

	/*
	 * Set the maximum key length and initialize the radix tree library.
	 * Tell rn_inithead() at which byte offset to find significant key
	 * bits.
	 */
	max_keylen = sizeof(struct bitval);
	rn_init();
	rn_inithead((void **)&op_rnh, offsetof(struct bitval, val));

	assert(op_rnh != NULL);

	/*
	 * Add a catch-all default opcode entry.
	 */
	op = opcode_alloc();
	op->bitmask = strdup("");
	op->mneumonic = strdup("(unknown)");
	op->detail = NULL;
	op->node.match.len = op->node.mask.len = 0;
	op_rnh->rnh_addaddr(&op->node.match, &op->node.mask, op_rnh,
			    (void *)op);

	/* Clear our per-region use flags. */
	memset(region_type_use, 0, sizeof(region_type_use));
}


/*!
 * optree_insert() - Add opcode to tree.
 *
 *	@param	node		Pointer to node to add to tree.
 *
 *	@return	Boolean indicating whether or not the opcode was successfully
 *		added to the tree.
 */
bool
optree_insert(struct OpTreeNode *node)
{
	struct radix_node *rn;
	struct OpTreeNode *xnode;

	assert(node->match.len == sizeof(node->match) &&
	       node->mask.len == sizeof(node->mask));

	rn = op_rnh->rnh_addaddr(&node->match, &node->mask, op_rnh,
				 (void *)node);
	if (rn != NULL)
		return true;

	/*
	 * If we were unable to add the new entry, then another node with
	 * the same bitmask must already exist in the tree.  Find out what
	 * node it is so we can inform the user.
	 */

	xnode = optree_lookup(&node->match.val);
	assert (xnode != NULL && xnode != node);

#ifdef XXXX
//	if (strcmp(xop->mneumonic, op->mneumonic) != 0) {
		warn("opcodes %s and %s have the same bitmask \"%s\"",
		     op->mneumonic, xop->mneumonic, op->bitmask);
//	}
#endif

	return false;
}


/*!
 * optree_lookup() - Lookup opcode.
 *
 *	
 *
 *	@param	keyptr		Pointer to XXX.
 *
 */
struct OpTreeNode *
optree_lookup(const void *keyptr)
{
	struct bitval key;
	struct OpTreeNode *op;

	key.len = sizeof(key);
	memcpy(&key.val, keyptr, sizeof(key.val));

	op = (struct OpTreeNode *)op_rnh->rnh_lookup(&key, NULL, op_rnh);
	return op;
}


void
optree_update(target_t targ, region_t region, vm_offset_t pc, uint cycles)
{
	struct OpTreeNode *node;
	struct Prefix *prefix;
	struct Opcode *op;
	struct counter *c;
	region_type_t regiontype;
	prefixmask_t prefixmask = PREFIXMASK_EMPTY;
	uint32_t text;

	assert(region != NULL);

	regiontype = region_get_type(region);
	assert(regiontype < NUMREGIONTYPES);

	region_type_use[regiontype] = true;

	/*
	 * First, build mask of all prefixes before the opcode.
	 */
	for (;;) {
		text = 0;
		region_read(targ, region, pc, &text, sizeof(text));

		node = optree_lookup(&text);
		assert(node != NULL);
		if (node->type != PREFIX)
			break;

		prefix = (struct Prefix *)node;

		pc += prefix->len;
		prefixmask |= prefix->mask;
	}

	assert(node->type == OPCODE);
	op = (struct Opcode *)node;

	/*
	 * Locate the counter to update by its prefix mask.
	 */
	for (c = &op->count_head[regiontype]; c != NULL; c = c->next) {
		if (c->prefixmask == prefixmask)
			break;
	}

	/*
	 * If there is no existing counter for the current prefix mask,
	 * append a new counter to the end of the list.
	 */
	if (c == NULL) {
		c = calloc(1, sizeof(*c));
		if (c == NULL)
			fatal(EX_OSERR, "malloc: %m");
		op->count_end[regiontype]->next = c;
		c->next = NULL;
		c->prefixmask = prefixmask;
	}

	c->n++;
	c->cycles_total += cycles;
	if (cycles < c->cycles_min)
		c->cycles_min = cycles;
	else if (cycles > c->cycles_max)
		c->cycles_max = cycles;

	/*
	 * Warn about instructions which match the default opcode.
	 * In order to reduce verbosity, we only print the warning when
	 * the current program counter differs from the last program counter
	 * at which we found an unknown opcode.
	 */
	if (op->node.match.len == 0) {
		static vm_offset_t prevpc = 0;
		if (pc != prevpc) {
			warn("unknown opcode at pc 0x%08x: 0x%08x", pc, text);
			prevpc = pc;
		}
	}
}


void
optree_output_open(void)
{
	xmlOutputBufferPtr out;

	assert(opt_outfile != NULL);
	assert(writer == NULL);

	/*
	 * Open the output file for writing.  We keep the output file open
	 * across multiple calls, overwriting the contents of the file each
	 * time we are called (e.g. checkpointing).  We only truncate the
	 * output file when we first open the file, after that the file can
	 * only get longer each time we write it as we either find new
	 * instructions or the instruction counts grow.
	 */
	if (writer_fd < 0) {
		writer_fd = open(opt_outfile, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		if (writer_fd < 0) {
			fatal(EX_OSERR, "unable to open %s for writing: %m",
			      opt_outfile);
		}
	}

	lseek(writer_fd, 0, SEEK_SET);

	out = xmlOutputBufferCreateFd(writer_fd, NULL);
	if (out == NULL) {
		fatal(EX_CANTCREAT, "unable to open %s for writing: %m",
		      opt_outfile);
	}

	writer = xmlNewTextWriter(out);
	if (writer == NULL) {
		xmlOutputBufferClose(out);
		fatal(EX_CANTCREAT, "unable to open %s for writing: %m",
		      opt_outfile);
	}

	xmlTextWriterSetIndent(writer, 4);
}


void
optree_output(void)
{
	const struct Prefix *prefix;
	region_type_t regiontype;
	uint i;

	assert(writer != NULL);

	if (xmlTextWriterStartDocument(writer, NULL, "utf-8", NULL) < 0)
		fatal(EX_IOERR, "failed to write to %s: %m", opt_outfile);

	xmlTextWriterStartElement(writer, "dyntrace");

	/* First, output a list of prefixes. */
	for (i = 0; i < prefix_count; i++) {
		prefix = &prefix_index[i];
		xmlTextWriterStartElement(writer, "prefix");
		xmlTextWriterWriteAttribute(writer, "id",
					    prefix_string(prefix->mask));
		xmlTextWriterWriteAttribute(writer, "bitmask", prefix->bitmask);
		xmlTextWriterWriteAttribute(writer, "detail", prefix->detail);
		xmlTextWriterEndElement(writer /* prefix */);
	}

	/*
	 * Iterate through the region types, outputting the opcodes in each
	 * region.
	 */
	for (regiontype = 0; regiontype < NUMREGIONTYPES; regiontype++) {

		if (!region_type_use[regiontype])
			continue;

		xmlTextWriterStartElement(writer, "region");
		xmlTextWriterWriteAttribute(writer, "type",
					    region_type_name[regiontype]);

		op_rnh->rnh_walktree(op_rnh, optree_print_node, &regiontype);

		xmlTextWriterEndElement(writer /* "region */);
	}

	xmlTextWriterEndElement(writer /* "dyntrace" */);
	xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);

	writer = NULL;

	/* Ensure the results are written to disk. */
	fsync(writer_fd);
}


const char *
prefix_string(prefixmask_t prefixmask)
{
	static char buffer[100];
	size_t len;
	prefixmask_t checkmask;
	int id;

	/* No instruction prefix is the most common case. */
	if (prefixmask == 0)
		return "";

	len = 0;

	for (id = 0, checkmask = 1; prefixmask != 0; id++, checkmask <<= 1) {
		char idstr[3] = { 'A', '\0', '\0' };
		size_t idstrlen = 1;

		if ((prefixmask & checkmask) == 0)
			continue;
		prefixmask &= ~checkmask;

		if (id < 26)
			idstr[0] += id;
		else {
			idstr[1] = 'A' + id - 26;
			idstrlen++;
		}

		assert(len + idstrlen + 1 < sizeof(buffer));
		assert(idstrlen <= 2);

		if (len > 0)
			buffer[len++] = ',';

		buffer[len + 0] = idstr[0];
		buffer[len + 1] = idstr[1];
		len += idstrlen;
	}

	buffer[len] = '\0';
	return buffer;
}


int
optree_print_node(struct radix_node *rn, void *arg)
{
	const struct OpTreeNode *node = (struct OpTreeNode *)rn;
	const struct Opcode *op = (const struct Opcode *)node;
	region_type_t regiontype = *(const region_type_t *)arg;
	const struct counter *c;
	char buffer[32];

	if (node->type != OPCODE)
		return 0;

	c = &op->count_head[regiontype];

	/*
	 * If there is only a single counter for this opcode (the one embedded
	 * in the Opcode structure) and that counter has a zero count, then
	 * only output it if the printzero option was specified on the command
	 * line.
	 */
	if (c->n == 0 && c->next == NULL && !opt_printzero)
		return 0;

	xmlTextWriterStartElement(writer, "op");
	xmlTextWriterWriteAttribute(writer, "bitmask", op->bitmask);
	xmlTextWriterWriteAttribute(writer, "mneumonic", op->mneumonic);
	if (op->detail != NULL)
		xmlTextWriterWriteAttribute(writer, "detail", op->detail);

	for (; c != NULL; c = c->next) {
		xmlTextWriterStartElement(writer, "count");

		xmlTextWriterWriteAttribute(writer, "prefixes",
					    prefix_string(c->prefixmask));

		snprintf(buffer, sizeof(buffer), "%llu",
			 (unsigned long long)c->n);
		xmlTextWriterWriteAttribute(writer, "n", buffer);

		/* Only output cycle counts if we have them. */
		if (c->cycles_total == 0) {
			xmlTextWriterEndElement(writer /* "count" */);
			continue;
		}

		snprintf(buffer, sizeof(buffer), "%llu",
			 (unsigned long long)c->cycles_total);
		xmlTextWriterWriteAttribute(writer, "cycles", buffer);

		snprintf(buffer, sizeof(buffer), "%u", c->cycles_min);
		xmlTextWriterWriteAttribute(writer, "min", buffer);

		snprintf(buffer, sizeof(buffer), "%u", c->cycles_min);
		xmlTextWriterWriteAttribute(writer, "max", buffer);

		xmlTextWriterEndElement(writer /* "count" */);
	}

	xmlTextWriterEndElement(writer /* op */);

	return 0;
}


void
optree_parsefile(const char *filepath)
{
	xmlTextReaderPtr reader;
	int ret;

	if (op_rnh == NULL)
		optree_init();

	LIBXML_TEST_VERSION

	reader = xmlNewTextReaderFilename(filepath);
	if (reader == NULL)
		fatal(EX_NOINPUT, "unable to open %s for reading", filepath);

	while ((ret = xmlTextReaderRead(reader)) > 0) {
		xmlNode *node;

		if (xmlTextReaderNodeType(reader) != XML_ELEMENT_NODE)
			continue;

		node = xmlTextReaderExpand(reader);

		if (strcmp(node->name, "prefix") == 0)
			prefix_parse(node);

		if (strcmp(node->name, "op") == 0)
			opcode_parse(node);
	}

	if (ret != 0)
		fatal(EX_DATAERR, "failed to parse %s", filepath);

	xmlFreeTextReader(reader);
}



void
opcode_parse(xmlNode *node)
{
	const xmlAttr *attr;
	struct Opcode *op;

	op = opcode_alloc();

	for (attr = node->properties; attr != NULL; attr = attr->next) {
		const char *name = attr->name;
		const char *value = XML_GET_CONTENT(attr->children);

		if (strcmp(name, "bitmask") == 0)
			op->bitmask = strdup(value);
		else if (strcmp(name, "mneumonic") == 0)
			op->mneumonic = strdup(value);
		else if (strcmp(name, "detail") == 0)
			op->detail = strdup(value);
	}

	/*
	 * Verify the opcode looks complete.
	 */
	if (op->bitmask == NULL) {
		fatal(EX_DATAERR, "bitmask missing at %ld",
		      XML_GET_LINE(node));
	}
	if (op->mneumonic == NULL) {
		fatal(EX_DATAERR, "mneumonic missing at %ld",
		      XML_GET_LINE(node));
	}

	parse_bitmask(op->bitmask, &op->node.mask.val, &op->node.match.val);

	if (!optree_insert(&op->node)) {
		opcode_free(&op);
	}
}


struct Opcode *
opcode_alloc(void)
{
	struct Opcode *op;
	region_type_t regiontype;

	op = calloc(1, sizeof(*op));
	if (op == NULL)
		fatal(EX_OSERR, "malloc: %m");

	for (regiontype = 0; regiontype < NUMREGIONTYPES; regiontype++)
		op->count_end[regiontype] = &op->count_head[regiontype];

	op->node.type = OPCODE;
	op->node.mask.len = sizeof(op->node.mask);
	op->node.match.len = sizeof(op->node.match);

	return op;
}


void
opcode_free(struct Opcode **opp)
{
	struct Opcode *op = *opp;

	*opp = NULL;
	if (op->bitmask != NULL)
		free(op->bitmask);
	if (op->mneumonic != NULL)
		free(op->mneumonic);
	if (op->detail != NULL)
		free(op->detail);
	free(op);
}


void
prefix_parse(xmlNode *node)
{
	const xmlAttr *attr;
	struct Prefix *prefix;

	if (prefix_count >= MAX_PREFIXES) {
		fatal(EX_SOFTWARE, "cannot specify more than %u prefixes",
		      MAX_PREFIXES);
	}

	prefix = &prefix_index[prefix_count];

	for (attr = node->properties; attr != NULL; attr = attr->next) {
		const char *name = attr->name;
		const char *value = XML_GET_CONTENT(attr->children);

		if (strcmp(name, "bitmask") == 0)
			prefix->bitmask = strdup(value);
		else if (strcmp(name, "detail") == 0)
			prefix->detail = strdup(value);
	}

	/*
	 * Verify the prefix looks complete.
	 */
	if (prefix->bitmask == NULL) {
		fatal(EX_DATAERR, "bitmask missing at %ld",
		      XML_GET_LINE(node));
	}

	prefix->node.type = PREFIX;
	prefix->node.mask.len = sizeof(prefix->node.mask);
	prefix->node.match.len = sizeof(prefix->node.match);

	parse_bitmask(prefix->bitmask,
		      &prefix->node.mask.val, &prefix->node.match.val);

	if (!optree_insert(&prefix->node)) {
		prefix_free(&prefix);
		return;
	}

	prefix->len = (strlen(prefix->bitmask) + 7) / 8;
	prefix->id = prefix_count;
	prefix->mask = 1 << prefix_count;
	prefix_count++;
}


void
prefix_free(struct Prefix **prefixp)
{
	struct Prefix *prefix = *prefixp;

	*prefixp = NULL;

	if (prefix->bitmask != NULL)
		free(prefix->bitmask);
	if (prefix->detail != NULL)
		free(prefix->detail);
}


void
parse_bitmask(const char *bitstr, uint32_t *maskp, uint32_t *matchp)
{
	uint32_t mask;
	uint32_t match;
	uint32_t i;

	mask = match = 0;
	i = 1 << ((sizeof(i) * 8) - 1);		/* Set high bit. */

	while (*bitstr != '\0') {
		assert(i != 0);

		if (strchr("01xX", *bitstr) == NULL) {
			fatal(EX_DATAERR,
			      "character '%c' not allowed in bitstr", *bitstr);
		}

		if (tolower(*bitstr) != 'x')
			mask |= i;
		if (*bitstr == '1')
			match |= i;

		i >>= 1;
		bitstr++;
	}

	/*
	 * Since the opcodes are defined by a consecutive sequence of bits,
	 * undo any host byte ordering.
	 */
	*maskp = htonl(mask);
	*matchp = htonl(match);
}

