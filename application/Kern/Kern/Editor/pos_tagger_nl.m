/* pos_tagger_nl.m — app implementation of the pos_tagger.h seam.
 *
 * Backs part-of-speech tagging with Foundation's NSLinguisticTagger (lexical-
 * class scheme) — the same on-device models NLTagger wraps, and the same engine
 * iA Writer's syntax highlighting uses. Plain Objective-C with a synchronous,
 * C-callable entry point: tagging a line is sub-millisecond once warm, so it's
 * safe to call from the render thread; only the one-time model load (~100ms) is
 * costly, which pos_tagger_warm() pays up front. Not compiled into the tests —
 * they link tests/pos_tagger_fake.c instead. */
#import <Foundation/Foundation.h>
#import <string.h>
#import "pos_tagger.h"

static PosClass class_for_tag(NSLinguisticTag tag) {
  /* content words */
  if ([tag isEqualToString:NSLinguisticTagNoun])        return POS_NOUN;
  if ([tag isEqualToString:NSLinguisticTagVerb])        return POS_VERB;
  if ([tag isEqualToString:NSLinguisticTagAdjective])   return POS_ADJECTIVE;
  if ([tag isEqualToString:NSLinguisticTagAdverb])      return POS_ADVERB;
  /* function words — the glue the value palette dims */
  if ([tag isEqualToString:NSLinguisticTagConjunction]) return POS_CONJUNCTION;
  if ([tag isEqualToString:NSLinguisticTagDeterminer])  return POS_DETERMINER;
  if ([tag isEqualToString:NSLinguisticTagPreposition]) return POS_PREPOSITION;
  if ([tag isEqualToString:NSLinguisticTagPronoun])     return POS_PRONOUN;
  if ([tag isEqualToString:NSLinguisticTagParticle])    return POS_PARTICLE;
  return POS_OTHER;
}

/* One tagger reused across calls (the render thread is the only caller, so no
   synchronization is needed). Setting .string each call is cheap; constructing
   the tagger is what pays the model-load cost. */
static NSLinguisticTagger *shared_tagger(void) {
  static NSLinguisticTagger *t;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    t = [[NSLinguisticTagger alloc]
          initWithTagSchemes:@[ NSLinguisticTagSchemeLexicalClass ] options:0];
  });
  return t;
}

int pos_tag_line(const char *text, int len, PosSpan *out, int max) {
  if (len <= 0 || max <= 0) return 0;
  __block int count = 0;
  @autoreleasepool {
    NSString *s = [[NSString alloc] initWithBytes:text length:(NSUInteger)len
                                         encoding:NSUTF8StringEncoding];
    if (!s) return 0;
    NSLinguisticTagger *tagger = shared_tagger();
    tagger.string = s;
    NSLinguisticTaggerOptions opts = NSLinguisticTaggerOmitPunctuation |
                                     NSLinguisticTaggerOmitWhitespace |
                                     NSLinguisticTaggerOmitOther;
    [tagger enumerateTagsInRange:NSMakeRange(0, s.length)
                            unit:NSLinguisticTaggerUnitWord
                          scheme:NSLinguisticTagSchemeLexicalClass
                         options:opts
                      usingBlock:^(NSLinguisticTag tag, NSRange r, BOOL *stop) {
      PosClass cls = class_for_tag(tag);
      if (cls == POS_OTHER) return;
      if (count >= max) { *stop = YES; return; }
      /* NSLinguisticTagger ranges are in UTF-16 units; Kern columns are UTF-8
         byte offsets. Convert via the encoded length of the leading substring
         and the token itself. Lines are short, so the per-token cost is fine. */
      NSUInteger b0 = [[s substringToIndex:r.location]
                          lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
      NSUInteger b1 = b0 + [[s substringWithRange:r]
                              lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
      out[count].start = (int)b0;
      out[count].end   = (int)b1;
      out[count].cls   = (unsigned char)cls;
      count++;
    }];
  }
  return count;
}

void pos_tagger_warm(void) {
  static const char sample[] = "The quick brown fox jumps over the lazy dog.";
  PosSpan tmp[16];
  (void)pos_tag_line(sample, (int)strlen(sample), tmp, 16);
}
