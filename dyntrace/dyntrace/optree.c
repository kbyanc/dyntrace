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
 * $kbyanc: dyntrace/dyntrace/optree.c,v 1.2 2004/11/28 00:55:17 kbyanc Exp $
 */

#include <libxml/xmlreader.h>

#include <assert.h>
#include <ctype.h>
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

#include "dynprof.h"
#include "radix.h"

/*!
 * @file
 *
 * XXXX
 *	We use the same radix tree code that FreeBSD (and other 4.4BSD
 *	derivatives) uses for routing lookups.  This data structure is
 *	perfectly suited for matching opcode bit strings as it provides
 *	best-match lookups with masking.  Masking is especially useful as it
 *	allows for don't-care bits in opcode bit strings (a requirement for
 *	the x86 instruction set and possibly others).
 *
 *
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
 * @struct counter
 *
 *	@param	count
 *
 *	@param	cycles_total
 *
 *	@param	cycles_min
 *
 *	@param	cycles_max
 *
 */
struct counter {
	uint64_t	 count;
	uint64_t	 cycles_total;
	uint		 cycles_min;
	uint		 cycles_max;
};


struct OpClass;


/*!
 * @struct OpTreeNode
 *
 * Data structure representing a single opcode.  This is used as an entry in
 * the radix tree so the first 2 fields must be pointers to radix tree nodes
 * (simulating a BSD rtentry structure).
 *
 *	@param	rn		
 *
 *	@param	match
 *
 *	@param	mask
 */
struct OpTreeNode {
	struct radix_node rn[2];
	struct bitval	 match; 
	struct bitval	 mask;
	struct OpClass	*opclass;
};


/*!
 * @struct Opcode
 *
 *
 *
 *	@param	bitmask
 */
struct Opcode {
	struct OpTreeNode node;

	struct counter	 count;

	char		*bitmask;
	char		*mneumonic;
	char		*detail;

	// XXXX per-prefix count
};


struct Prefix {
	struct OpTreeNode node;

	uint		 id;
	char		*bitmask;
	char		*detail;
};

static struct radix_node_head *op_rnh = NULL;

static uint prefix_count = 0;

static void	 optree_init(void);
static bool	 optree_insert(struct OpTreeNode *op);
static struct OpTreeNode *optree_lookup(const void *keyptr);
static int	 optree_print_node(struct radix_node *rn, void *arg);

static void	 opcode_parse(xmlNode *node);
static void	 opcode_print(const struct OpTreeNode *node, FILE *f);
static void	 opcode_free(struct Opcode **opp);

static void	 prefix_parse(xmlNode *node);
static void	 prefix_print(const struct OpTreeNode *node, FILE *f);
static void	 prefix_free(struct Prefix **prefixp);

static void	 parse_bitmask(const char *bitstr,
			       uint32_t *mask, uint32_t *match);

struct OpClass {
	const char *typename;
	void (*print)(const struct OpTreeNode *op, FILE *f);
//	void (*free)(struct OpTreeNode **opp);
//	void (*accept)(struct OpTreeNode *op);
};
static struct OpClass opcode_class = {
	"opcode",
	opcode_print,
//	opcode_accept
};
static struct OpClass prefix_class = {
	"prefix",
	prefix_print,
};


/*!
 * optree_init() - Initialize radix tree routines for use as opcode lookup tree.
 */
void
optree_init(void)
{

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
}


/*!
 * optree_insert() - Add opcode to tree.
 *
 *	@param	op		Pointer to opcode to add to tree.
 *
 *	@return	Boolean indicating whether or not the opcode was successfully
 *		added to the tree.
 */
bool
optree_insert(struct OpTreeNode *op)
{
	struct radix_node *rn;
	struct OpTreeNode *xop;

	assert(op->opclass != NULL);
	assert(op->match.len == sizeof(op->match) &&
	       op->mask.len == sizeof(op->mask));

	rn = op_rnh->rnh_addaddr(&op->match, &op->mask, op_rnh, (void *)op);
	if (rn != NULL)
		return (true);

	/*
	 * If we were unable to add the new entry, then another opcode with
	 * the same bitmask must already exist in the tree.  Find out what
	 * opcode it is so we can inform the user.
	 */

	xop = optree_lookup(&op->match.val);
	assert (xop != NULL && xop != op);

#ifdef XXXX
//	if (strcmp(xop->mneumonic, op->mneumonic) != 0) {
		warn("opcodes %s and %s have the same bitmask \"%s\"",
		     op->mneumonic, xop->mneumonic, op->bitmask);
//	}
#endif

	return (false);
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
	return (op);
}


int
optree_print_node(struct radix_node *rn, void *arg)
{
	const struct OpTreeNode *node = (struct OpTreeNode *)rn;
	FILE *f = arg;

	node->opclass->print(node, f);
	return (0);
}


void
optree_output(FILE *f)
{
	op_rnh->rnh_walktree(op_rnh, optree_print_node, f);
}


#if 0
void
opcode_update(const void *pc, unsigned int cycles)
{
	struct Opcode *op;

	op = (struct Opcode *)optree_lookup(pc);
	if (op == NULL) {
		/* XXX No match??? */
		return;
	}

	op->count++;
	op->cycles_total += cycles;
	if (cycles < op->cycles_min)
		op->cycles_min = cycles;
	else if (cycles > op->cycles_max)
		op->cycles_max = cycles;
}
#endif


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

	/* XXXXXXXXTEMP */
	optree_output(stdout);
}




void
opcode_parse(xmlNode *node)
{
	const xmlAttr *attr;
	struct Opcode *op;

	op = calloc(1, sizeof(*op));
	if (op == NULL)
		fatal(EX_OSERR, "malloc: %m");

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

	op->node.opclass = &opcode_class;
	op->node.mask.len = sizeof(op->node.mask);
	op->node.match.len = sizeof(op->node.match);

	parse_bitmask(op->bitmask, &op->node.mask.val, &op->node.match.val);

	if (!optree_insert(&op->node)) {
		opcode_free(&op);
	}
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
opcode_print(const struct OpTreeNode *node, FILE *f)
{
	const struct Opcode *op = (const struct Opcode *)node;
	fprintf(f, "OPCODE %-32s\t%s\n", op->bitmask, op->mneumonic);
}



void
prefix_parse(xmlNode *node)
{
	const xmlAttr *attr;
	struct Prefix *prefix;

	if (prefix_count >= sizeof(prefix->id) * 8) {
		fatal(EX_SOFTWARE, "cannot specify more than %u prefixes",
		      sizeof(prefix->id) * 8);
	}

	prefix = calloc(1, sizeof(*prefix));
	if (prefix == NULL)
		fatal(EX_OSERR, "malloc: %m");

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

	prefix->node.opclass = &prefix_class;
	prefix->node.mask.len = sizeof(prefix->node.mask);
	prefix->node.match.len = sizeof(prefix->node.match);

	parse_bitmask(prefix->bitmask,
		      &prefix->node.mask.val, &prefix->node.match.val);

	if (!optree_insert(&prefix->node)) {
		prefix_free(&prefix);
		return;
	}

	prefix->id = 1 << prefix_count++;
}


void
prefix_free(struct Prefix **prefixp)
{
	struct Prefix *prefix = *prefixp;

	*prefixp = NULL;
#if 0
	if (op->bitmask != NULL)
		free(op->bitmask);
	if (op->mneumonic != NULL)
		free(op->mneumonic);
	if (op->detail != NULL)
		free(op->detail);
#endif
	free(prefix);
}


void
prefix_print(const struct OpTreeNode *node, FILE *f)
{
	const struct Prefix *prefix = (const struct Prefix *)node;
	fprintf(f, "PREFIX %-32s\t%x\n", prefix->bitmask, prefix->id);
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

